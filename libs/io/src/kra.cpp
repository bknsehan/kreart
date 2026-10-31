#include "kreart/io/kra.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <zip.h>

#include "kreart/io/png.h"
#include "kreart/io/timelapse.h"
#include "lzf.h"

namespace kreart::io {
namespace {

constexpr int kKritaTileSize = 64;
constexpr int kKritaTilePixels = kKritaTileSize * kKritaTileSize;
constexpr std::size_t kKritaTileBytes = kKritaTilePixels * 4;

struct ParsedLayer {
  std::string name;
  std::string filename;
  std::string nodetype = "paintlayer";
  std::string compositeop = "normal";
  double opacity = 1.0;
  bool visible = true;
  bool locked = false;
  bool selected = false;
  bool inherit_alpha = false;
  int offset_x = 0;
  int offset_y = 0;
};

std::string get_prop(xmlNodePtr node, const char *name, const std::string &fallback = "") {
  xmlChar *val = xmlGetProp(node, reinterpret_cast<const xmlChar *>(name));
  if (!val) {
    return fallback;
  }
  std::string s(reinterpret_cast<const char *>(val));
  xmlFree(val);
  return s;
}
inline int parse_int_safe(const std::string &str, int fallback = 0) {
  try {
    std::size_t pos = 0;
    int val = std::stoi(str, &pos);
    return val;
  } catch (...) {
    return fallback;
  }
}


std::vector<std::uint8_t> read_zip_entry(zip_t *za, const std::string &name) {
  zip_stat_t st;
  zip_stat_init(&st);
  if (zip_stat(za, name.c_str(), 0, &st) != 0) {
    return {};
  }

  zip_file_t *zf = zip_fopen(za, name.c_str(), 0);
  if (!zf) {
    return {};
  }

  std::vector<std::uint8_t> buffer(st.size);
  zip_int64_t total = 0;
  while (total < static_cast<zip_int64_t>(st.size)) {
    const zip_int64_t bytes =
        zip_fread(zf, buffer.data() + total, st.size - static_cast<std::size_t>(total));
    if (bytes <= 0) {
      break;
    }
    total += bytes;
  }
  zip_fclose(zf);
  buffer.resize(static_cast<std::size_t>(std::max(zip_int64_t{0}, total)));
  return buffer;
}

std::string find_zip_entry_ending_with(zip_t *za, const std::string &suffix) {
  const zip_int64_t num_entries = zip_get_num_entries(za, 0);
  for (zip_int64_t i = 0; i < num_entries; ++i) {
    const char *entry_name = zip_get_name(za, static_cast<zip_uint64_t>(i), 0);
    if (!entry_name) continue;
    std::string s(entry_name);
    if (s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return s;
    }
  }
  return "";
}

} // namespace

bool read_kra(const std::filesystem::path &path, core::Document &document, std::string &error,
              const std::string &session_uuid) {
  int zip_err = 0;
  zip_t *za = zip_open(path.string().c_str(), ZIP_RDONLY, &zip_err);
  if (!za) {
    zip_error_t zerr;
    zip_error_init_with_code(&zerr, zip_err);
    error = "Failed to open KRA file: " + std::string(zip_error_strerror(&zerr));
    zip_error_fini(&zerr);
    return false;
  }

  // Read maindoc.xml
  const std::vector<std::uint8_t> maindoc_data = read_zip_entry(za, "maindoc.xml");
  if (maindoc_data.empty()) {
    zip_close(za);
    error = "maindoc.xml not found in KRA archive";
    return false;
  }

  xmlDocPtr doc = xmlReadMemory(reinterpret_cast<const char *>(maindoc_data.data()),
                                static_cast<int>(maindoc_data.size()), "maindoc.xml", nullptr,
                                XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
  if (!doc) {
    zip_close(za);
    error = "Failed to parse maindoc.xml";
    return false;
  }

  xmlNodePtr root = xmlDocGetRootElement(doc);
  if (!root) {
    xmlFreeDoc(doc);
    zip_close(za);
    error = "Empty maindoc.xml";
    return false;
  }

  // Find <IMAGE>
  xmlNodePtr image_node = nullptr;
  for (xmlNodePtr cur = root->children; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE && xmlStrcmp(cur->name, reinterpret_cast<const xmlChar *>("IMAGE")) == 0) {
      image_node = cur;
      break;
    }
  }

  if (!image_node) {
    xmlFreeDoc(doc);
    zip_close(za);
    error = "No <IMAGE> element found in maindoc.xml";
    return false;
  }

  const int doc_width = std::max(1, parse_int_safe(get_prop(image_node, "width", "100"), 100));
  const int doc_height = std::max(1, parse_int_safe(get_prop(image_node, "height", "100"), 100));
  const std::string image_name = get_prop(image_node, "name", "kreart");

  // Parse layers in <layers>
  std::vector<ParsedLayer> parsed_layers;
  xmlNodePtr layers_node = nullptr;
  for (xmlNodePtr cur = image_node->children; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE && xmlStrcmp(cur->name, reinterpret_cast<const xmlChar *>("layers")) == 0) {
      layers_node = cur;
      break;
    }
  }

  if (layers_node) {
    for (xmlNodePtr cur = layers_node->children; cur; cur = cur->next) {
      if (cur->type != XML_ELEMENT_NODE) continue;
      const std::string tag = reinterpret_cast<const char *>(cur->name);
      if (tag == "layer") {
        ParsedLayer pl;
        pl.name = get_prop(cur, "name", "Layer");
        pl.filename = get_prop(cur, "filename", "");
        pl.nodetype = get_prop(cur, "nodetype", "paintlayer");
        pl.opacity = std::clamp(parse_int_safe(get_prop(cur, "opacity", "255"), 255) / 255.0, 0.0, 1.0);
        pl.visible = get_prop(cur, "visible", "1") != "0";
        pl.locked = get_prop(cur, "locked", "0") != "0";
        pl.selected = get_prop(cur, "selected", "false") == "true";
        pl.offset_x = parse_int_safe(get_prop(cur, "x", "0"), 0);
        pl.offset_y = parse_int_safe(get_prop(cur, "y", "0"), 0);
        pl.inherit_alpha = get_prop(cur, "inherit_alpha", "0") == "1";
        parsed_layers.push_back(std::move(pl));
      }
    }
  }

  xmlFreeDoc(doc);

  if (parsed_layers.empty()) {
    zip_close(za);
    error = "No layers found in KRA document";
    return false;
  }

  // In maindoc.xml, layers are stored top to bottom.
  // In Document, layer 0 is bottom-most. Reverse parsed layers.
  std::reverse(parsed_layers.begin(), parsed_layers.end());

  document.reset(doc_width, doc_height);
  // Clear the default Background layer created by reset
  while (document.layer_count() > 0) {
    if (!document.remove_layer(0)) {
      break;
    }
  }

  std::vector<std::uint8_t> decomp_tile(kKritaTileBytes);
  std::size_t selected_index = parsed_layers.size() - 1;

  for (std::size_t i = 0; i < parsed_layers.size(); ++i) {
    const auto &pl = parsed_layers[i];
    if (pl.selected) {
      selected_index = i;
    }

    core::Layer layer(pl.name, doc_width, doc_height, false);
    layer.set_visible(pl.visible);
    layer.set_opacity(pl.opacity);
    layer.set_composite_op(pl.compositeop);
    layer.set_locked(pl.locked);
    if (pl.nodetype == "grouplayer") {
      layer.set_is_group(true);
    }
    layer.set_clipping_mask(pl.inherit_alpha);

    // Look for layer file in zip
    std::string layer_entry = image_name + "/layers/" + pl.filename;
    std::vector<std::uint8_t> layer_bytes = read_zip_entry(za, layer_entry);
    if (layer_bytes.empty()) {
      layer_entry = find_zip_entry_ending_with(za, "/layers/" + pl.filename);
      if (!layer_entry.empty()) {
        layer_bytes = read_zip_entry(za, layer_entry);
      }
    }

    if (!layer_bytes.empty()) {
      // Parse Krita VERSION 2 tile data
      std::size_t pos = 0;
      auto read_line = [&]() -> std::string {
        if (pos >= layer_bytes.size()) return "";
        const auto nl = std::find(layer_bytes.begin() + pos, layer_bytes.end(), '\n');
        std::string line(layer_bytes.begin() + pos, nl);
        pos = (nl == layer_bytes.end()) ? layer_bytes.size() : (nl - layer_bytes.begin() + 1);
        return line;
      };

      int tile_count = 0;
      while (pos < layer_bytes.size()) {
        std::string line = read_line();
        if (line.rfind("DATA", 0) == 0) {
          tile_count = std::atoi(line.substr(5).c_str());
          break;
        }
      }

      for (int t = 0; t < tile_count && pos < layer_bytes.size(); ++t) {
        std::string tile_line = read_line();
        if (tile_line.empty()) break;

        // Parse: x,y,comp,length
        int tx = 0;
        int ty = 0;
        char comp[16] = {0};
        int length = 0;
        if (std::sscanf(tile_line.c_str(), "%d,%d,%15[^,],%d", &tx, &ty, comp, &length) != 4) {
          break;
        }

        if (pos + static_cast<std::size_t>(length) > layer_bytes.size()) {
          break;
        }

        const std::uint8_t *raw_tile = layer_bytes.data() + pos;
        pos += length;

        if (length < 1) continue;

        const std::uint8_t flag = raw_tile[0];
        const std::uint8_t *payload = raw_tile + 1;
        const std::size_t payload_len = length - 1;

        bool decompressed = false;
        if (flag == 1 && std::strcmp(comp, "LZF") == 0) {
          const std::size_t decomp_size =
              lzf_decompress(payload, payload_len, decomp_tile.data(), decomp_tile.size());
          if (decomp_size == kKritaTileBytes) {
            decompressed = true;
          }
        } else if (flag == 0 && payload_len == kKritaTileBytes) {
          std::memcpy(decomp_tile.data(), payload, kKritaTileBytes);
          decompressed = true;
        }

        if (!decompressed) {
          if (!error.empty()) error += "; ";
          error += "Corrupted tile in layer '" + pl.name + "' skipped";
          continue;
        }

        // Planar BGRA -> Kreart 128x128 tiles
        // tx, ty are document pixel offsets (multiples of 64)
        for (int ly = 0; ly < kKritaTileSize; ++ly) {
          const int py = ty + ly;
          if (py < 0 || py >= doc_height) continue;
          for (int lx = 0; lx < kKritaTileSize; ++lx) {
            const int px = tx + lx;
            if (px < 0 || px >= doc_width) continue;

            const int p = ly * kKritaTileSize + lx;
            const std::uint8_t b = decomp_tile[p];
            const std::uint8_t g = decomp_tile[4096 + p];
            const std::uint8_t r = decomp_tile[8192 + p];
            const std::uint8_t a = decomp_tile[12288 + p];

            layer.image().set_pixel(px, py, core::Rgba8{r, g, b, a});
          }
        }
      }
    }

    if (i == 0 && document.layer_count() == 1) {
      // Replace the initial background placeholder
      document.layer(0) = std::move(layer);
    } else {
      document.insert_layer(i, std::move(layer));
    }
  }
  // Extract embedded timelapse if present
  const std::string tl_prefix = "kreart/timelapse/";
  std::filesystem::path target_tl_dir;
  if (!session_uuid.empty()) {
    target_tl_dir = TimelapseRecorder::get_session_dir_for(session_uuid);
  }

  const zip_int64_t num_entries = zip_get_num_entries(za, 0);
  for (zip_int64_t e = 0; e < num_entries; ++e) {
    const char *ename = zip_get_name(za, e, 0);
    if (!ename) continue;
    std::string sname(ename);
    if (sname.rfind(tl_prefix, 0) == 0) {
      std::string sub = sname.substr(tl_prefix.size());
      if (sub.empty() || sub.back() == '/') continue;

      if (target_tl_dir.empty()) {
        target_tl_dir = TimelapseRecorder::get_session_dir_for("imported");
      }
      std::error_code ec;
      std::filesystem::create_directories(target_tl_dir, ec);

      std::vector<std::uint8_t> bytes = read_zip_entry(za, sname);
      if (!bytes.empty()) {
        std::ofstream ofs(target_tl_dir / sub, std::ios::binary);
        if (ofs.is_open()) {
          ofs.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
      }
    }
  }

  zip_close(za);
  document.set_active_layer_index(selected_index);
  document.mark_composite_dirty();
  return true;
}

bool read_kra_thumbnail(const std::filesystem::path &path,
                        std::vector<std::uint8_t> &out_png_bytes,
                        int &out_width, int &out_height,
                        std::string &error) {
  return read_kra_thumbnail(path, out_png_bytes, out_width, out_height, error, true);
}

bool read_kra_thumbnail(const std::filesystem::path &path,
                        std::vector<std::uint8_t> &out_png_bytes,
                        int &out_width, int &out_height,
                        std::string &error, bool allow_full_load) {
  out_png_bytes.clear();
  out_width = 1920;
  out_height = 1080;
  int zip_err = 0;
  zip_t *za = zip_open(path.string().c_str(), ZIP_RDONLY, &zip_err);
  if (!za) {
    error = "Could not open KRA file as zip";
    return false;
  }

  const std::vector<std::uint8_t> maindoc_data = read_zip_entry(za, "maindoc.xml");
  if (!maindoc_data.empty()) {
    xmlDocPtr doc = xmlReadMemory(reinterpret_cast<const char *>(maindoc_data.data()),
                                  static_cast<int>(maindoc_data.size()), "maindoc.xml", nullptr,
                                  XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
    if (doc) {
      xmlNodePtr root = xmlDocGetRootElement(doc);
      if (root) {
        for (xmlNodePtr cur = root->children; cur; cur = cur->next) {
          if (cur->type == XML_ELEMENT_NODE &&
              xmlStrcmp(cur->name, reinterpret_cast<const xmlChar *>("IMAGE")) == 0) {
            out_width = parse_int_safe(get_prop(cur, "width", "1920"), 1920);
            out_height = parse_int_safe(get_prop(cur, "height", "1080"), 1080);
            break;
          }
        }
      }
      xmlFreeDoc(doc);
    }
  }

  out_png_bytes = read_zip_entry(za, "preview.png");
  if (out_png_bytes.empty()) {
    out_png_bytes = read_zip_entry(za, "mergedimage.png");
  }

  zip_close(za);

  if (out_png_bytes.empty() && allow_full_load) {
    core::Document doc(1, 1);
    std::string rerr;
    if (read_kra(path, doc, rerr)) {
      out_width = doc.width();
      out_height = doc.height();
      const std::filesystem::path temp_png =
          std::filesystem::temp_directory_path() / ("kreart_thumb_" + std::to_string(std::rand()) + ".png");
      std::string perr;
      doc.mark_composite_dirty();
      if (write_png(temp_png, doc.composite_with_floating(), perr)) {
        std::ifstream f(temp_png, std::ios::binary);
        if (f) {
          out_png_bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        std::filesystem::remove(temp_png);
      }
    }
  }

  if (out_png_bytes.empty()) {
    error = allow_full_load ? "No preview or mergedimage found in KRA file" : "No preview found";
    return false;
  }
  return true;
}

bool read_kra_thumbnail(const std::filesystem::path &path,
                        std::vector<std::uint8_t> &out_png_bytes,
                        std::string &error) {
  int w = 0, h = 0;
  return read_kra_thumbnail(path, out_png_bytes, w, h, error);
}

bool write_kra(const std::filesystem::path &path, const core::Document &document,
               std::string &error, const std::string &session_uuid) {
  const std::filesystem::path temp_path = path.string() + ".tmp";
  int zip_err = 0;
  zip_t *za = zip_open(temp_path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_err);
  if (!za) {
    zip_error_t zerr;
    zip_error_init_with_code(&zerr, zip_err);
    error = "Failed to create KRA file: " + std::string(zip_error_strerror(&zerr));
    zip_error_fini(&zerr);
    return false;
  }

  // 1. mimetype (uncompressed)
  const char mimetype[] = "application/x-kra";
  zip_source_t *s_mime = zip_source_buffer(za, mimetype, sizeof(mimetype) - 1, 0);
  if (!s_mime) {
    zip_close(za);
    error = "Failed to create mimetype zip source";
    return false;
  }
  const zip_int64_t mime_idx = zip_file_add(za, "mimetype", s_mime, ZIP_FL_OVERWRITE);
  zip_set_file_compression(za, mime_idx, ZIP_CM_STORE, 0);

  // 2. maindoc.xml
  const std::string doc_name = "kreart";
  std::ostringstream xml;
  xml.imbue(std::locale::classic());
  xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  xml << "<!DOCTYPE DOC PUBLIC '-//KDE//DTD krita 2.0//EN' 'http://www.calligra.org/DTD/krita-2.0.dtd'>\n";
  xml << "<DOC xmlns=\"http://www.calligra.org/DTD/krita\" syntaxVersion=\"2.0\" editor=\"Krita\">\n";
  xml << " <IMAGE width=\"" << document.width() << "\" height=\"" << document.height()
      << "\" mime=\"application/x-kra\" name=\"" << doc_name
      << "\" colorspacename=\"RGBA\" profile=\"sRGB built-in\" x-res=\"100\" y-res=\"100\">\n";
  xml << "  <layers>\n";

  // Layers written top to bottom
  const std::size_t num_layers = document.layer_count();
  for (std::size_t i = num_layers; i > 0; --i) {
    const std::size_t idx = i - 1;
    const auto &l = document.layer(idx);
    const int op_val = std::clamp(static_cast<int>(l.opacity() * 255.0 + 0.5), 0, 255);
    const std::string nodetype = l.is_group() ? "grouplayer" : "paintlayer";
    xml << "   <layer name=\"" << l.name() << "\" filename=\"layer" << (idx + 1)
        << "\" nodetype=\"" << nodetype << "\" opacity=\"" << op_val << "\" visible=\""
        << (l.visible() ? "1" : "0") << "\" compositeop=\"" << l.composite_op()
        << "\" locked=\"" << (l.locked() ? "1" : "0") << "\"";
    if (l.clipping_mask()) {
      xml << " inherit_alpha=\"1\"";
    }
    if (idx == document.active_layer_index()) {
      xml << " selected=\"true\"";
    }
    xml << " x=\"0\" y=\"0\" colorspacename=\"RGBA\"/>\n";
  }
  xml << "  </layers>\n";
  xml << " </IMAGE>\n";
  xml << "</DOC>\n";

  const std::string xml_str = xml.str();
  zip_source_t *s_xml = zip_source_buffer(za, xml_str.data(), xml_str.size(), 0);
  zip_file_add(za, "maindoc.xml", s_xml, ZIP_FL_OVERWRITE);

  // 3. documentinfo.xml
  const std::string docinfo =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE document-info PUBLIC \"-//KDE//DTD document-info 1.0//EN\" "
      "\"http://www.calligra.org/DTD/document-info-1.0.dtd\">\n"
      "<document-info xmlns=\"http://www.calligra.org/DTD/document-info\">\n"
      " <about><title>Kreart Document</title></about>\n"
      "</document-info>\n";
  zip_source_t *s_info = zip_source_buffer(za, docinfo.data(), docinfo.size(), 0);
  zip_file_add(za, "documentinfo.xml", s_info, ZIP_FL_OVERWRITE);

  // 4. mergedimage.png and preview.png
  // Write to a temporary file using existing write_png, read into buffer
  const std::filesystem::path temp_png =
      std::filesystem::temp_directory_path() / ("kreart_merged_" + std::to_string(std::rand()) + ".png");
  std::string png_err;
  std::vector<std::uint8_t> png_bytes;
  const_cast<core::Document &>(document).mark_composite_dirty();
  if (write_png(temp_png, const_cast<core::Document &>(document).composite_with_floating(), png_err)) {
    std::ifstream f(temp_png, std::ios::binary);
    if (f) {
      png_bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(temp_png);
  }

  if (!png_bytes.empty()) {
    zip_source_t *s_merged = zip_source_buffer(za, png_bytes.data(), png_bytes.size(), 0);
    zip_file_add(za, "mergedimage.png", s_merged, ZIP_FL_OVERWRITE);
    zip_source_t *s_preview = zip_source_buffer(za, png_bytes.data(), png_bytes.size(), 0);
    zip_file_add(za, "preview.png", s_preview, ZIP_FL_OVERWRITE);
  }

  // 5. Layer tile data files
  // We keep buffers alive until zip_close
  std::vector<std::vector<std::uint8_t>> layer_buffers(num_layers);
  std::vector<std::uint8_t> raw_planar(kKritaTileBytes);
  std::vector<std::uint8_t> comp_tile(kKritaTileBytes * 2);

  for (std::size_t idx = 0; idx < num_layers; ++idx) {
    const auto &l = document.layer(idx);
    if (l.is_group()) {
      continue;
    }
    std::vector<std::uint8_t> &lbuf = layer_buffers[idx];

    // Collect all 64x64 tiles
    struct TileEntry {
      int tx;
      int ty;
      std::vector<std::uint8_t> compressed;
    };
    std::vector<TileEntry> tiles;

    const int num_k_cols = (document.width() + kKritaTileSize - 1) / kKritaTileSize;
    const int num_k_rows = (document.height() + kKritaTileSize - 1) / kKritaTileSize;

    for (int kty = 0; kty < num_k_rows; ++kty) {
      const int ty = kty * kKritaTileSize;
      for (int ktx = 0; ktx < num_k_cols; ++ktx) {
        const int tx = ktx * kKritaTileSize;

        // Check if any pixel exists in this 64x64 quadrant
        bool has_pixels = false;
        for (int ly = 0; ly < kKritaTileSize; ++ly) {
          const int py = ty + ly;
          if (py >= document.height()) break;
          for (int lx = 0; lx < kKritaTileSize; ++lx) {
            const int px = tx + lx;
            if (px >= document.width()) break;
            const core::Rgba8 px_color = l.image().pixel(px, py);
            const int p = ly * kKritaTileSize + lx;
            raw_planar[p] = px_color.b;
            raw_planar[4096 + p] = px_color.g;
            raw_planar[8192 + p] = px_color.r;
            raw_planar[12288 + p] = px_color.a;
            if (px_color.a > 0 || px_color.r > 0 || px_color.g > 0 || px_color.b > 0) {
              has_pixels = true;
            }
          }
        }

        if (!has_pixels) continue;

        // Compress tile with LZF
        const std::size_t comp_len =
            lzf_compress(raw_planar.data(), raw_planar.size(), comp_tile.data(), comp_tile.size());

        TileEntry entry;
        entry.tx = tx;
        entry.ty = ty;
        if (comp_len > 0 && comp_len < kKritaTileBytes) {
          entry.compressed.resize(comp_len + 1);
          entry.compressed[0] = 1; // LZF compressed
          std::memcpy(entry.compressed.data() + 1, comp_tile.data(), comp_len);
        } else {
          entry.compressed.resize(kKritaTileBytes + 1);
          entry.compressed[0] = 0; // Uncompressed
          std::memcpy(entry.compressed.data() + 1, raw_planar.data(), kKritaTileBytes);
        }
        tiles.push_back(std::move(entry));
      }
    }

    // Write header
    std::ostringstream hdr;
    hdr.imbue(std::locale::classic());
    hdr << "VERSION 2\nTILEWIDTH 64\nTILEHEIGHT 64\nPIXELSIZE 4\nDATA " << tiles.size() << "\n";
    const std::string hdr_str = hdr.str();
    lbuf.insert(lbuf.end(), hdr_str.begin(), hdr_str.end());

    // Write tiles
    for (const auto &tile : tiles) {
      char thdr[64];
      std::snprintf(thdr, sizeof(thdr), "%d,%d,LZF,%zu\n", tile.tx, tile.ty,
                    tile.compressed.size());
      lbuf.insert(lbuf.end(), thdr, thdr + std::strlen(thdr));
      lbuf.insert(lbuf.end(), tile.compressed.begin(), tile.compressed.end());
    }

    const std::string layer_path = doc_name + "/layers/layer" + std::to_string(idx + 1);
    zip_source_t *s_layer = zip_source_buffer(za, lbuf.data(), lbuf.size(), 0);
    zip_file_add(za, layer_path.c_str(), s_layer, ZIP_FL_OVERWRITE);

    // defaultpixel
    static const std::uint8_t def_pixel[4] = {0, 0, 0, 0};
    zip_source_t *s_def = zip_source_buffer(za, def_pixel, sizeof(def_pixel), 0);
    const std::string def_path = layer_path + ".defaultpixel";
    zip_file_add(za, def_path.c_str(), s_def, ZIP_FL_OVERWRITE);
  }
  // Packaging timelapse frames if present
  std::vector<std::vector<std::uint8_t>> tl_file_buffers;
  if (!session_uuid.empty()) {
    std::filesystem::path tl_dir = TimelapseRecorder::get_session_dir_for(session_uuid);
    TimelapseMetadata meta;
    if (TimelapseRecorder::load_metadata(tl_dir, meta) && meta.frame_count > 0) {
      std::filesystem::path meta_file = tl_dir / "metadata.json";
      std::ifstream mifs(meta_file, std::ios::binary);
      if (mifs.is_open()) {
        std::vector<std::uint8_t> mbytes((std::istreambuf_iterator<char>(mifs)),
                                          std::istreambuf_iterator<char>());
        if (!mbytes.empty()) {
          tl_file_buffers.push_back(std::move(mbytes));
          zip_source_t *s_meta = zip_source_buffer(za, tl_file_buffers.back().data(), tl_file_buffers.back().size(), 0);
          zip_file_add(za, "kreart/timelapse/metadata.json", s_meta, ZIP_FL_OVERWRITE);
        }
      }

      for (int f = 0; f < meta.frame_count; ++f) {
        char fbuf[32];
        std::snprintf(fbuf, sizeof(fbuf), "frame_%06d.jpg", f);
        std::filesystem::path fpath = tl_dir / fbuf;
        std::ifstream fifs(fpath, std::ios::binary);
        if (fifs.is_open()) {
          std::vector<std::uint8_t> fbytes((std::istreambuf_iterator<char>(fifs)),
                                            std::istreambuf_iterator<char>());
          if (!fbytes.empty()) {
            tl_file_buffers.push_back(std::move(fbytes));
            zip_source_t *s_f = zip_source_buffer(za, tl_file_buffers.back().data(), tl_file_buffers.back().size(), 0);
            std::string entry_name = "kreart/timelapse/" + std::string(fbuf);
            zip_file_add(za, entry_name.c_str(), s_f, ZIP_FL_OVERWRITE);
          }
        }
      }
    }
  }

  if (zip_close(za) < 0) {
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);
    error = "Failed to finalize KRA zip file";
    return false;
  }

  std::error_code ec;
  std::filesystem::rename(temp_path, path, ec);
  if (ec) {
    std::filesystem::copy_file(temp_path, path, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(temp_path, ec);
    if (ec) {
      error = "Failed to replace target KRA file: " + ec.message();
      return false;
    }
  }

  return true;
}

} // namespace kreart::io
