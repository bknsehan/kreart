#include "kreart/io/wal_journal.h"
#include "lzf.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <system_error>

#include <QStandardPaths>
#include <QString>

namespace kreart::io {

namespace {

std::filesystem::path get_cache_journal_dir() {
  const char *xdg_cache = std::getenv("XDG_CACHE_HOME");
  std::filesystem::path base;
  if (xdg_cache && *xdg_cache) {
    base = xdg_cache;
  } else {
    QString qcache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (!qcache.isEmpty()) {
      base = qcache.toStdString();
    } else {
      const char *home = std::getenv("HOME");
      base = home ? std::filesystem::path(home) / ".cache" : std::filesystem::temp_directory_path();
    }
  }
  std::filesystem::path journal_dir = base / "kreart" / "journal";
  std::error_code ec;
  std::filesystem::create_directories(journal_dir, ec);
  return journal_dir;
}

template <typename T>
void append_to_buffer(std::vector<std::uint8_t> &buf, const T &val) {
  const auto *p = reinterpret_cast<const std::uint8_t *>(&val);
  buf.insert(buf.end(), p, p + sizeof(T));
}

void append_bytes_to_buffer(std::vector<std::uint8_t> &buf, const void *data, std::size_t len) {
  const auto *p = reinterpret_cast<const std::uint8_t *>(data);
  buf.insert(buf.end(), p, p + len);
}

} // namespace

std::uint32_t WalJournal::calculate_crc32(const std::uint8_t *data, std::size_t length) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
      }
      t[i] = c;
    }
    return t;
  }();

  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t i = 0; i < length; ++i) {
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFU;
}

bool WalJournal::uuid_to_bytes(const std::string &uuid_str, std::uint8_t out_bytes[16]) {
  std::memset(out_bytes, 0, 16);
  std::string hex_only;
  hex_only.reserve(32);
  for (char c : uuid_str) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
      hex_only.push_back(c);
    }
  }
  if (hex_only.size() != 32) {
    // Hash string into 16 bytes as fallback
    std::size_t h1 = std::hash<std::string>{}(uuid_str);
    std::size_t h2 = std::hash<std::string>{}(uuid_str + "_salt");
    std::memcpy(out_bytes, &h1, sizeof(h1));
    std::memcpy(out_bytes + sizeof(h1), &h2, sizeof(h2));
    return false;
  }

  for (std::size_t i = 0; i < 16; ++i) {
    char byte_str[3] = {hex_only[i * 2], hex_only[i * 2 + 1], '\0'};
    out_bytes[i] = static_cast<std::uint8_t>(std::strtoul(byte_str, nullptr, 16));
  }
  return true;
}

std::string WalJournal::bytes_to_uuid(const std::uint8_t bytes[16]) {
  char buf[37];
  std::snprintf(buf, sizeof(buf),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3],
                bytes[4], bytes[5],
                bytes[6], bytes[7],
                bytes[8], bytes[9],
                bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(buf);
}

std::filesystem::path WalJournal::default_journal_path_for(const std::filesystem::path &project_path,
                                                          const std::string &session_uuid) {
  if (project_path.empty()) {
    std::string uuid = session_uuid.empty() ? "untitled" : session_uuid;
    return get_cache_journal_dir() / (uuid + ".wal");
  }

  std::filesystem::path companion =
      project_path.parent_path() / ("." + project_path.filename().string() + ".wal");

  std::error_code ec;
  // Test if companion directory is writable
  if (std::filesystem::exists(project_path.parent_path(), ec)) {
    return companion;
  }

  // Fallback to cache directory
  std::size_t path_hash = std::hash<std::string>{}(project_path.string());
  return get_cache_journal_dir() / (std::to_string(path_hash) + ".wal");
}

WalJournal::WalJournal() = default;

WalJournal::~WalJournal() {
  close();
}

bool WalJournal::open(const std::filesystem::path &journal_path, int doc_width, int doc_height,
                      const std::string &session_uuid) {
  close();

  journal_path_ = journal_path;
  session_uuid_ = session_uuid;

  std::error_code ec;
  std::filesystem::create_directories(journal_path_.parent_path(), ec);

  file_ = std::fopen(journal_path_.string().c_str(), "wb+");
  if (!file_) {
    return false;
  }

  WalHeader header;
  header.magic = kWALMagic;
  header.version = kWALVersion;
  header.flags = 0;
  header.doc_width = static_cast<std::uint32_t>(doc_width);
  header.doc_height = static_cast<std::uint32_t>(doc_height);
  uuid_to_bytes(session_uuid_, header.session_id);

  if (std::fwrite(&header, 1, sizeof(header), file_) != sizeof(header)) {
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  std::fflush(file_);

  stop_worker_ = false;
  is_open_ = true;
  worker_thread_ = std::thread(&WalJournal::worker_loop, this);
  return true;
}

void WalJournal::close() {
  if (!is_open_ && !worker_thread_.joinable()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    stop_worker_ = true;
  }
  queue_cv_.notify_all();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  if (file_) {
    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
  }
  is_open_ = false;
}

void WalJournal::checkpoint(std::uint32_t base_file_crc32) {
  if (!is_open_) return;

  PendingRecord record;
  record.type = WalRecordType::CHECKPOINT;
  record.timestamp_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
  record.checkpoint_crc = base_file_crc32;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_front_.push_back(std::move(record));
  }
  queue_cv_.notify_one();
  flush_sync();
}

void WalJournal::record_stroke(const core::DocumentChangeEvent &stroke_event,
                              const std::vector<CapturedTile> &captured_tiles) {
  if (!is_open_) return;

  PendingRecord record;
  record.type = WalRecordType::STROKE_EVENT;
  record.layer_id = stroke_event.layer_id;
  record.timestamp_ms = stroke_event.timestamp_ms;
  record.brush_settings = stroke_event.brush_settings;
  record.stroke_points = stroke_event.stroke_points;
  record.tiles = captured_tiles;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_front_.push_back(std::move(record));
  }
  queue_cv_.notify_one();
}

void WalJournal::record_tile_diff(std::size_t layer_id,
                                 const std::vector<CapturedTile> &captured_tiles,
                                 std::uint64_t timestamp_ms) {
  if (!is_open_ || captured_tiles.empty()) return;

  if (timestamp_ms == 0) {
    timestamp_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
  }

  PendingRecord record;
  record.type = WalRecordType::TILE_DIFF;
  record.layer_id = layer_id;
  record.timestamp_ms = timestamp_ms;
  record.tiles = captured_tiles;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_front_.push_back(std::move(record));
  }
  queue_cv_.notify_one();
}

void WalJournal::record_layer_mutation(core::DocumentChangeType op_type, std::size_t layer_id,
                                      const std::string &metadata, std::uint64_t timestamp_ms) {
  if (!is_open_) return;

  if (timestamp_ms == 0) {
    timestamp_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
  }

  PendingRecord record;
  record.type = WalRecordType::LAYER_MUTATION;
  record.mutation_op = op_type;
  record.layer_id = layer_id;
  record.timestamp_ms = timestamp_ms;
  record.metadata = metadata;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_front_.push_back(std::move(record));
  }
  queue_cv_.notify_one();
}

void WalJournal::flush_sync() {
  if (!is_open_) return;

  std::unique_lock<std::mutex> lock(queue_mutex_);
  flush_cv_.wait(lock, [this] {
    return queue_front_.empty() && queue_back_.empty();
  });
}

void WalJournal::write_record_bytes(WalRecordType type, std::uint64_t timestamp_ms,
                                    const std::vector<std::uint8_t> &payload) {
  if (!file_) return;

  WalChunkHeader chunk;
  chunk.record_magic = kWALRecordMagic;
  chunk.record_type = static_cast<std::uint16_t>(type);
  chunk.payload_size = static_cast<std::uint32_t>(payload.size());
  chunk.crc32 = calculate_crc32(payload.data(), payload.size());
  chunk.timestamp_ms = timestamp_ms;

  std::fwrite(&chunk, 1, sizeof(chunk), file_);
  if (!payload.empty()) {
    std::fwrite(payload.data(), 1, payload.size(), file_);
  }
}

void WalJournal::worker_loop() {
  std::vector<std::uint8_t> lzf_buffer;
  lzf_buffer.resize(sizeof(core::Tile) + 1024);

  while (true) {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] {
        return !queue_front_.empty() || stop_worker_;
      });

      if (queue_front_.empty() && stop_worker_) {
        break;
      }

      std::swap(queue_front_, queue_back_);
    }

    for (const auto &record : queue_back_) {
      if (record.type == WalRecordType::STROKE_EVENT) {
        // 1. Write STROKE_EVENT record
        std::vector<std::uint8_t> payload;
        append_to_buffer(payload, static_cast<std::uint32_t>(record.layer_id));
        append_to_buffer(payload, static_cast<std::int32_t>(record.brush_settings.mode));
        append_to_buffer(payload, record.brush_settings.size);
        append_to_buffer(payload, record.brush_settings.opacity);
        append_to_buffer(payload, record.brush_settings.flow);
        append_to_buffer(payload, record.brush_settings.hardness);
        append_to_buffer(payload, record.brush_settings.spacing);
        append_to_buffer(payload, record.brush_settings.color.r);
        append_to_buffer(payload, record.brush_settings.color.g);
        append_to_buffer(payload, record.brush_settings.color.b);
        append_to_buffer(payload, record.brush_settings.color.a);

        std::uint16_t op_len = static_cast<std::uint16_t>(record.brush_settings.composite_op.size());
        append_to_buffer(payload, op_len);
        if (op_len > 0) {
          append_bytes_to_buffer(payload, record.brush_settings.composite_op.data(), op_len);
        }

        std::uint32_t pt_count = static_cast<std::uint32_t>(record.stroke_points.size());
        append_to_buffer(payload, pt_count);
        for (const auto &pt : record.stroke_points) {
          append_to_buffer(payload, pt.x);
          append_to_buffer(payload, pt.y);
          append_to_buffer(payload, pt.pressure);
          append_to_buffer(payload, pt.tilt_x);
          append_to_buffer(payload, pt.tilt_y);
          append_to_buffer(payload, pt.timestamp_ms);
        }

        write_record_bytes(WalRecordType::STROKE_EVENT, record.timestamp_ms, payload);

        // 2. If stroke has modified tile diffs, write companion TILE_DIFF record
        if (!record.tiles.empty()) {
          std::vector<std::uint8_t> diff_payload;
          append_to_buffer(diff_payload, static_cast<std::uint32_t>(record.layer_id));
          append_to_buffer(diff_payload, static_cast<std::uint32_t>(record.tiles.size()));

          for (const auto &t : record.tiles) {
            append_to_buffer(diff_payload, static_cast<std::int32_t>(t.tx));
            append_to_buffer(diff_payload, static_cast<std::int32_t>(t.ty));

            if (!t.tile) {
              std::uint32_t comp_len = 0;
              append_to_buffer(diff_payload, comp_len);
            } else {
              const auto *in_bytes = reinterpret_cast<const std::uint8_t *>(t.tile->pixels.data());
              std::size_t in_len = sizeof(t.tile->pixels);
              std::size_t comp = lzf_compress(in_bytes, in_len, lzf_buffer.data(), lzf_buffer.size());
              std::uint32_t comp_len = static_cast<std::uint32_t>(comp);
              append_to_buffer(diff_payload, comp_len);
              append_bytes_to_buffer(diff_payload, lzf_buffer.data(), comp);
            }
          }
          write_record_bytes(WalRecordType::TILE_DIFF, record.timestamp_ms, diff_payload);
        }
      } else if (record.type == WalRecordType::TILE_DIFF) {
        std::vector<std::uint8_t> diff_payload;
        append_to_buffer(diff_payload, static_cast<std::uint32_t>(record.layer_id));
        append_to_buffer(diff_payload, static_cast<std::uint32_t>(record.tiles.size()));

        for (const auto &t : record.tiles) {
          append_to_buffer(diff_payload, static_cast<std::int32_t>(t.tx));
          append_to_buffer(diff_payload, static_cast<std::int32_t>(t.ty));

          if (!t.tile) {
            std::uint32_t comp_len = 0;
            append_to_buffer(diff_payload, comp_len);
          } else {
            const auto *in_bytes = reinterpret_cast<const std::uint8_t *>(t.tile->pixels.data());
            std::size_t in_len = sizeof(t.tile->pixels);
            std::size_t comp = lzf_compress(in_bytes, in_len, lzf_buffer.data(), lzf_buffer.size());
            std::uint32_t comp_len = static_cast<std::uint32_t>(comp);
            append_to_buffer(diff_payload, comp_len);
            append_bytes_to_buffer(diff_payload, lzf_buffer.data(), comp);
          }
        }
        write_record_bytes(WalRecordType::TILE_DIFF, record.timestamp_ms, diff_payload);
      } else if (record.type == WalRecordType::LAYER_MUTATION) {
        std::vector<std::uint8_t> payload;
        append_to_buffer(payload, static_cast<std::uint16_t>(record.mutation_op));
        append_to_buffer(payload, static_cast<std::uint32_t>(record.layer_id));
        std::uint32_t meta_len = static_cast<std::uint32_t>(record.metadata.size());
        append_to_buffer(payload, meta_len);
        if (meta_len > 0) {
          append_bytes_to_buffer(payload, record.metadata.data(), meta_len);
        }
        write_record_bytes(WalRecordType::LAYER_MUTATION, record.timestamp_ms, payload);
      } else if (record.type == WalRecordType::CHECKPOINT) {
        std::vector<std::uint8_t> payload;
        append_to_buffer(payload, record.timestamp_ms);
        append_to_buffer(payload, record.checkpoint_crc);
        write_record_bytes(WalRecordType::CHECKPOINT, record.timestamp_ms, payload);

        // Update header flag to 1 (checkpointed)
        if (file_) {
          std::fflush(file_);
          std::uint16_t flag = 1;
          std::fseek(file_, 6, SEEK_SET);
          std::fwrite(&flag, 1, sizeof(flag), file_);
          std::fseek(file_, 0, SEEK_END);
        }
      }
    }

    if (file_) {
      std::fflush(file_);
    }

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      queue_back_.clear();
    }
    flush_cv_.notify_all();
  }
}

} // namespace kreart::io
