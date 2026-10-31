#pragma once

#include <cstddef>
#include <cstdint>

namespace kreart::io {

// Marc Lehmann's LZF compression algorithm.
// Returns compressed size in bytes, or 0 if output buffer is too small.
std::size_t lzf_compress(const std::uint8_t *in_data, std::size_t in_len,
                         std::uint8_t *out_data, std::size_t max_out);

// Returns decompressed size in bytes, or 0 on error / buffer too small.
std::size_t lzf_decompress(const std::uint8_t *in_data, std::size_t in_len,
                           std::uint8_t *out_data, std::size_t max_out);

} // namespace kreart::io
