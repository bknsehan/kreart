#include "lzf.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace kreart::io {
namespace {

constexpr std::size_t kHLog = 14;
constexpr std::size_t kHSize = (1 << kHLog);

} // namespace

std::size_t lzf_compress(const std::uint8_t *in_data, std::size_t in_len,
                         std::uint8_t *out_data, std::size_t max_out) {
  if (in_len == 0 || max_out == 0) {
    return 0;
  }

  std::array<std::size_t, kHSize> htab{};
  std::size_t ip = 0;
  std::size_t op = 0;
  std::size_t lit = 0;

  auto flush_lit = [&]() -> bool {
    if (lit == 0) {
      return true;
    }
    if (op + lit + 1 > max_out) {
      return false;
    }
    out_data[op++] = static_cast<std::uint8_t>(lit - 1);
    std::memcpy(&out_data[op], &in_data[ip - lit], lit);
    op += lit;
    lit = 0;
    return true;
  };

  if (in_len < 3) {
    lit = in_len;
    ip = in_len;
    if (!flush_lit()) {
      return 0;
    }
    return op;
  }

  std::uint32_t hval = (static_cast<std::uint32_t>(in_data[0]) << 8) | in_data[1];
  while (ip < in_len - 2) {
    hval = ((hval << 8) | in_data[ip + 2]) & 0xffffffU;
    const std::size_t hslot =
        (((hval ^ (hval << 5)) >> (24 - kHLog)) - hval * 5) & (kHSize - 1);
    const std::size_t ref = htab[hslot];
    htab[hslot] = ip;

    const std::size_t off = ip > ref ? (ip - ref - 1) : 0;
    if (off < 8192 && ref > 0 && ip + 2 < in_len && in_data[ref] == in_data[ip] &&
        in_data[ref + 1] == in_data[ip + 1] && in_data[ref + 2] == in_data[ip + 2]) {
      if (!flush_lit()) {
        return 0;
      }

      std::size_t match_len = 3;
      const std::size_t max_len = std::min(in_len - ip, static_cast<std::size_t>(264));
      while (match_len < max_len && in_data[ref + match_len] == in_data[ip + match_len]) {
        ++match_len;
      }

      const std::size_t length = match_len - 2;
      ip += match_len;

      if (length < 7) {
        if (op + 2 > max_out) return 0;
        out_data[op++] = static_cast<std::uint8_t>((length << 5) | (off >> 8));
      } else {
        if (op + 3 > max_out) return 0;
        out_data[op++] = static_cast<std::uint8_t>((7 << 5) | (off >> 8));
        out_data[op++] = static_cast<std::uint8_t>(length - 7);
      }
      out_data[op++] = static_cast<std::uint8_t>(off & 0xff);

      if (ip < in_len - 2) {
        hval = (static_cast<std::uint32_t>(in_data[ip]) << 8) | in_data[ip + 1];
      }
    } else {
      ++lit;
      ++ip;
      if (lit == 32) {
        if (!flush_lit()) {
          return 0;
        }
      }
    }
  }

  while (ip < in_len) {
    ++lit;
    ++ip;
    if (lit == 32) {
      if (!flush_lit()) {
        return 0;
      }
    }
  }

  if (!flush_lit()) {
    return 0;
  }
  return op;
}

std::size_t lzf_decompress(const std::uint8_t *in_data, std::size_t in_len,
                           std::uint8_t *out_data, std::size_t max_out) {
  std::size_t ip = 0;
  std::size_t op = 0;

  while (ip < in_len) {
    const std::uint32_t ctrl = in_data[ip++];
    if (ctrl < 32) {
      const std::size_t len = ctrl + 1;
      if (ip + len > in_len || op + len > max_out) {
        return 0;
      }
      std::memcpy(&out_data[op], &in_data[ip], len);
      ip += len;
      op += len;
    } else {
      std::size_t len = ctrl >> 5;
      std::size_t ref_offset = ((ctrl & 0x1f) << 8) + 1;
      if (len == 7) {
        if (ip >= in_len) return 0;
        len += in_data[ip++];
      }
      if (ip >= in_len) return 0;
      ref_offset += in_data[ip++];
      len += 2;

      if (ref_offset > op || op + len > max_out) {
        return 0;
      }
      std::size_t ref = op - ref_offset;
      for (std::size_t k = 0; k < len; ++k) {
        out_data[op++] = out_data[ref++];
      }
    }
  }

  return op;
}

} // namespace kreart::io
