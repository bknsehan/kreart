#include "kreart/brush/brush_tip.h"

#include <algorithm>
#include <cstring>
#include <png.h>

namespace kreart::brush {
namespace {

std::uint32_t read_u32_be(const std::uint8_t *p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

struct PngReaderState {
  const std::uint8_t *data = nullptr;
  std::size_t size = 0;
  std::size_t offset = 0;
};

void png_read_callback(png_structp png_ptr, png_bytep out_bytes, png_size_t bytes_to_read) {
  auto *state = static_cast<PngReaderState *>(png_get_io_ptr(png_ptr));
  if (!state || state->offset + bytes_to_read > state->size) {
    png_error(png_ptr, "End of memory buffer");
    return;
  }
  std::memcpy(out_bytes, state->data + state->offset, bytes_to_read);
  state->offset += bytes_to_read;
}

} // namespace

BrushTip::BrushTip(int width, int height, std::vector<std::uint8_t> mask)
    : width_(width), height_(height), mask_(std::move(mask)) {}


std::shared_ptr<BrushTip> BrushTip::load_gbr(const std::uint8_t *data, std::size_t size) {
  if (!data || size < 28) {
    return nullptr;
  }

  const std::uint32_t header_size = read_u32_be(data);
  const std::uint32_t width = read_u32_be(data + 8);
  const std::uint32_t height = read_u32_be(data + 12);
  const std::uint32_t bpp = read_u32_be(data + 16);
  const std::uint32_t magic = read_u32_be(data + 20);

  // Magic 0x47494D50 == "GIMP"
  if (magic != 0x47494D50 || header_size >= size || width == 0 || height == 0) {
    return nullptr;
  }

  const std::size_t num_pixels = static_cast<std::size_t>(width) * height;
  if (header_size + num_pixels * bpp > size) {
    return nullptr;
  }

  std::vector<std::uint8_t> mask(num_pixels);
  const std::uint8_t *pixel_data = data + header_size;

  if (bpp == 1) {
    std::memcpy(mask.data(), pixel_data, num_pixels);
  } else if (bpp == 4) {
    for (std::size_t i = 0; i < num_pixels; ++i) {
      mask[i] = pixel_data[i * 4 + 3]; // Alpha channel
    }
  } else {
    for (std::size_t i = 0; i < num_pixels; ++i) {
      mask[i] = pixel_data[i * bpp];
    }
  }

  return std::make_shared<BrushTip>(static_cast<int>(width), static_cast<int>(height),
                                    std::move(mask));
}

std::shared_ptr<BrushTip> BrushTip::load_gih(const std::uint8_t *data, std::size_t size) {
  if (!data || size < 28) {
    return nullptr;
  }

  // Scan for GIMP magic within the GIH container
  for (std::size_t i = 20; i + 8 <= size; ++i) {
    if (data[i] == 'G' && data[i + 1] == 'I' && data[i + 2] == 'M' && data[i + 3] == 'P') {
      if (i >= 20) {
        const std::size_t gbr_start = i - 20;
        auto tip = load_gbr(data + gbr_start, size - gbr_start);
        if (tip) {
          return tip;
        }
      }
    }
  }

  return nullptr;
}

std::shared_ptr<BrushTip> BrushTip::load_png(const std::uint8_t *data, std::size_t size) {
  if (!data || size < 8 || png_sig_cmp(data, 0, 8) != 0) {
    return nullptr;
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    return nullptr;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    return nullptr;
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    return nullptr;
  }

  PngReaderState state{data, size, 0};
  png_set_read_fn(png, &state, png_read_callback);
  png_read_info(png, info);

  const png_uint_32 width = png_get_image_width(png, info);
  const png_uint_32 height = png_get_image_height(png, info);
  const png_byte color_type = png_get_color_type(png, info);
  const png_byte bit_depth = png_get_bit_depth(png, info);

  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }

  png_read_update_info(png, info);

  const std::size_t row_bytes = png_get_rowbytes(png, info);
  std::vector<std::uint8_t> image_data(row_bytes * height);
  std::vector<png_bytep> row_pointers(height);
  for (png_uint_32 y = 0; y < height; ++y) {
    row_pointers[y] = image_data.data() + y * row_bytes;
  }

  png_read_image(png, row_pointers.data());
  png_destroy_read_struct(&png, &info, nullptr);

  const int channels = static_cast<int>(row_bytes / width);
  const std::size_t num_pixels = static_cast<std::size_t>(width) * height;
  std::vector<std::uint8_t> mask(num_pixels);

  for (png_uint_32 y = 0; y < height; ++y) {
    for (png_uint_32 x = 0; x < width; ++x) {
      const std::size_t p = static_cast<std::size_t>(y) * width + x;
      const std::size_t src_offset = static_cast<std::size_t>(y) * row_bytes + x * channels;

      if (channels == 1) {
        // Grayscale mask: inverted in Krita (white is empty, dark is dab)
        mask[p] = 255 - image_data[src_offset];
      } else if (channels == 2) {
        mask[p] = image_data[src_offset + 1]; // Gray + Alpha
      } else if (channels >= 4) {
        mask[p] = image_data[src_offset + 3]; // RGBA
      } else {
        // RGB without alpha: use darkness as dab intensity
        const std::uint8_t r = image_data[src_offset];
        const std::uint8_t g = image_data[src_offset + 1];
        const std::uint8_t b = image_data[src_offset + 2];
        const std::uint8_t luma = static_cast<std::uint8_t>((r * 299 + g * 587 + b * 114) / 1000);
        mask[p] = 255 - luma;
      }
    }
  }

  return std::make_shared<BrushTip>(static_cast<int>(width), static_cast<int>(height),
                                    std::move(mask));
}

std::shared_ptr<BrushTip> BrushTip::load_from_memory(const std::uint8_t *data, std::size_t size,
                                                     std::string_view filename) {
  if (filename.ends_with(".gbr")) {
    return load_gbr(data, size);
  }
  if (filename.ends_with(".gih")) {
    return load_gih(data, size);
  }
  if (filename.ends_with(".png") || (size >= 8 && png_sig_cmp(data, 0, 8) == 0)) {
    return load_png(data, size);
  }

  // Fallback probing
  if (auto tip = load_gbr(data, size)) {
    return tip;
  }
  if (auto tip = load_png(data, size)) {
    return tip;
  }
  return load_gih(data, size);
}

} // namespace kreart::brush
