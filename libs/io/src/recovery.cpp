#include "kreart/io/recovery.h"
#include "kreart/io/wal_journal.h"
#include "lzf.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace kreart::io {

namespace {

template <typename T>
bool read_from_payload(const std::vector<std::uint8_t> &buf, std::size_t &pos, T &out_val) {
  if (pos + sizeof(T) > buf.size()) return false;
  std::memcpy(&out_val, buf.data() + pos, sizeof(T));
  pos += sizeof(T);
  return true;
}

bool read_bytes_from_payload(const std::vector<std::uint8_t> &buf, std::size_t &pos,
                             void *out_dst, std::size_t len) {
  if (pos + len > buf.size()) return false;
  std::memcpy(out_dst, buf.data() + pos, len);
  pos += len;
  return true;
}

} // namespace

bool inspect_wal(const std::filesystem::path &wal_path, WalRecoveryStats &stats, std::string &error) {
  stats = WalRecoveryStats{};

  FILE *f = std::fopen(wal_path.string().c_str(), "rb");
  if (!f) {
    error = "Could not open WAL file: " + wal_path.string();
    return false;
  }

  WalHeader header;
  if (std::fread(&header, 1, sizeof(header), f) != sizeof(header)) {
    std::fclose(f);
    error = "Failed to read WAL header";
    return false;
  }

  if (header.magic != kWALMagic) {
    std::fclose(f);
    error = "Invalid WAL header magic (expected KWAL)";
    return false;
  }

  stats.doc_width = static_cast<int>(header.doc_width);
  stats.doc_height = static_cast<int>(header.doc_height);
  stats.session_uuid = WalJournal::bytes_to_uuid(header.session_id);
  if (header.flags == 1) {
    stats.reached_clean_checkpoint = true;
  }

  std::vector<std::uint8_t> payload;

  while (true) {
    WalChunkHeader chunk;
    std::size_t n = std::fread(&chunk, 1, sizeof(chunk), f);
    if (n == 0) {
      // Clean EOF
      break;
    }
    if (n < sizeof(chunk)) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    if (chunk.record_magic != kWALRecordMagic) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    if (chunk.payload_size > 100 * 1024 * 1024) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    payload.resize(chunk.payload_size);
    if (chunk.payload_size > 0) {
      if (std::fread(payload.data(), 1, chunk.payload_size, f) != chunk.payload_size) {
        stats.reached_eof_or_corrupt_tail = true;
        break;
      }
    }

    std::uint32_t crc = WalJournal::calculate_crc32(payload.data(), payload.size());
    if (crc != chunk.crc32) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    auto rec_type = static_cast<WalRecordType>(chunk.record_type);
    if (rec_type == WalRecordType::STROKE_EVENT) {
      ++stats.stroke_events_replayed;
    } else if (rec_type == WalRecordType::TILE_DIFF) {
      ++stats.tile_diffs_replayed;
    } else if (rec_type == WalRecordType::LAYER_MUTATION) {
      ++stats.mutations_applied;
    } else if (rec_type == WalRecordType::CHECKPOINT) {
      stats.reached_clean_checkpoint = true;
    }
    ++stats.total_records_processed;
  }

  std::fclose(f);
  return true;
}

bool has_uncommitted_wal(const std::filesystem::path &project_path, std::filesystem::path &out_wal_path) {
  std::filesystem::path companion = WalJournal::default_journal_path_for(project_path);
  std::error_code ec;
  if (!std::filesystem::exists(companion, ec)) {
    return false;
  }

  WalRecoveryStats stats;
  std::string err;
  if (!inspect_wal(companion, stats, err)) {
    return false;
  }

  if (stats.total_records_processed > 0 && !stats.reached_clean_checkpoint) {
    out_wal_path = companion;
    return true;
  }
  return false;
}

bool recover_document_from_wal(const std::filesystem::path &wal_path,
                               core::Document &document,
                               std::string &out_report,
                               WalRecoveryStats *out_stats) {
  WalRecoveryStats stats;
  FILE *f = std::fopen(wal_path.string().c_str(), "rb");
  if (!f) {
    out_report = "Failed to open WAL file: " + wal_path.string();
    return false;
  }

  WalHeader header;
  if (std::fread(&header, 1, sizeof(header), f) != sizeof(header)) {
    std::fclose(f);
    out_report = "Failed to read WAL header";
    return false;
  }

  if (header.magic != kWALMagic) {
    std::fclose(f);
    out_report = "Invalid WAL header magic (not KWAL)";
    return false;
  }

  stats.doc_width = static_cast<int>(header.doc_width);
  stats.doc_height = static_cast<int>(header.doc_height);
  stats.session_uuid = WalJournal::bytes_to_uuid(header.session_id);
  if (header.flags == 1) {
    stats.reached_clean_checkpoint = true;
  }

  if (document.width() <= 0 || document.height() <= 0 ||
      (document.width() != stats.doc_width && document.height() != stats.doc_height)) {
    document.reset(stats.doc_width, stats.doc_height);
  }

  std::vector<std::uint8_t> payload;
  std::vector<std::uint8_t> decomp_tile(sizeof(core::Tile));

  while (true) {
    WalChunkHeader chunk;
    std::size_t n = std::fread(&chunk, 1, sizeof(chunk), f);
    if (n == 0) {
      break;
    }
    if (n < sizeof(chunk)) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    if (chunk.record_magic != kWALRecordMagic) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    if (chunk.payload_size > 100 * 1024 * 1024) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    payload.resize(chunk.payload_size);
    if (chunk.payload_size > 0) {
      if (std::fread(payload.data(), 1, chunk.payload_size, f) != chunk.payload_size) {
        stats.reached_eof_or_corrupt_tail = true;
        break;
      }
    }

    std::uint32_t crc = WalJournal::calculate_crc32(payload.data(), payload.size());
    if (crc != chunk.crc32) {
      stats.reached_eof_or_corrupt_tail = true;
      break;
    }

    auto rec_type = static_cast<WalRecordType>(chunk.record_type);
    std::size_t pos = 0;

    if (rec_type == WalRecordType::STROKE_EVENT) {
      // STROKE_EVENT carries stroke points; actual pixel diffs follow or are in companion TILE_DIFF
      ++stats.stroke_events_replayed;
    } else if (rec_type == WalRecordType::TILE_DIFF) {
      std::uint32_t layer_id = 0;
      std::uint32_t tile_count = 0;
      if (read_from_payload(payload, pos, layer_id) &&
          read_from_payload(payload, pos, tile_count)) {
        while (document.layer_count() <= layer_id) {
          document.add_layer("Layer " + std::to_string(document.layer_count() + 1));
        }

        auto &layer = document.layer(layer_id);
        for (std::uint32_t i = 0; i < tile_count; ++i) {
          std::int32_t tx = 0;
          std::int32_t ty = 0;
          std::uint32_t comp_len = 0;
          if (!read_from_payload(payload, pos, tx) ||
              !read_from_payload(payload, pos, ty) ||
              !read_from_payload(payload, pos, comp_len)) {
            break;
          }

          if (comp_len == 0) {
            layer.image().remove_tile(tx, ty);
          } else {
            if (pos + comp_len > payload.size()) {
              break;
            }
            const std::uint8_t *comp_bytes = payload.data() + pos;
            pos += comp_len;

            std::size_t decomp_size =
                lzf_decompress(comp_bytes, comp_len, decomp_tile.data(), decomp_tile.size());
            if (decomp_size == sizeof(core::Tile)) {
              auto *tile = layer.image().writable_tile_at(tx, ty);
              std::memcpy(tile->pixels.data(), decomp_tile.data(), sizeof(tile->pixels));
            }
          }
        }
        ++stats.tile_diffs_replayed;
      }
    } else if (rec_type == WalRecordType::LAYER_MUTATION) {
      std::uint16_t op_type = 0;
      std::uint32_t layer_id = 0;
      std::uint32_t meta_len = 0;
      if (read_from_payload(payload, pos, op_type) &&
          read_from_payload(payload, pos, layer_id) &&
          read_from_payload(payload, pos, meta_len)) {
        if (op_type == static_cast<std::uint16_t>(core::DocumentChangeType::LAYER_ADDED)) {
          if (document.layer_count() <= layer_id) {
            document.add_layer("Layer " + std::to_string(document.layer_count() + 1));
          }
        }
        ++stats.mutations_applied;
      }
    } else if (rec_type == WalRecordType::CHECKPOINT) {
      stats.reached_clean_checkpoint = true;
    }

    ++stats.total_records_processed;
  }

  std::fclose(f);

  if (out_stats) {
    *out_stats = stats;
  }

  if (stats.total_records_processed == 0) {
    out_report = "WAL file contained no recoverable records.";
    return false;
  }

  document.mark_composite_dirty();

  std::ostringstream oss;
  oss << "Reconstructed project from journal (" << stats.total_records_processed
      << " records: " << stats.stroke_events_replayed << " strokes, "
      << stats.tile_diffs_replayed << " tile diffs).";
  if (stats.reached_eof_or_corrupt_tail) {
    oss << " (Crash point encountered; partial record discarded safely).";
  }
  out_report = oss.str();
  return true;
}

} // namespace kreart::io
