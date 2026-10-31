#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "kreart/core/tracker_events.h"
#include "kreart/core/tiled_image.h"

namespace kreart::io {

inline constexpr std::uint32_t kWALMagic = 0x4C41574B;       // "KWAL"
inline constexpr std::uint32_t kWALRecordMagic = 0x524C4157; // "WALR"
inline constexpr std::uint16_t kWALVersion = 1;

enum class WalRecordType : std::uint16_t {
  STROKE_EVENT = 1,
  TILE_DIFF = 2,
  LAYER_MUTATION = 3,
  CHECKPOINT = 4
};

#pragma pack(push, 1)
struct WalHeader {
  std::uint32_t magic = kWALMagic;
  std::uint16_t version = kWALVersion;
  std::uint16_t flags = 0; // 0 = active, 1 = checkpointed
  std::uint32_t doc_width = 0;
  std::uint32_t doc_height = 0;
  std::uint8_t session_id[16]{};
};

struct WalChunkHeader {
  std::uint32_t record_magic = kWALRecordMagic;
  std::uint16_t record_type = 0;
  std::uint32_t payload_size = 0;
  std::uint32_t crc32 = 0;
  std::uint64_t timestamp_ms = 0;
};
#pragma pack(pop)

static_assert(sizeof(WalHeader) == 32, "WalHeader must be exactly 32 bytes");
static_assert(sizeof(WalChunkHeader) == 22, "WalChunkHeader must be exactly 22 bytes");

struct CapturedTile {
  int tx = 0;
  int ty = 0;
  std::shared_ptr<core::Tile> tile; // null if tile is empty/deleted
};

class WalJournal {
public:
  WalJournal();
  ~WalJournal();

  WalJournal(const WalJournal &) = delete;
  WalJournal &operator=(const WalJournal &) = delete;

  bool open(const std::filesystem::path &journal_path, int doc_width, int doc_height,
            const std::string &session_uuid);
  void close();
  void checkpoint(std::uint32_t base_file_crc32 = 0);

  // Enqueue operations from main thread (non-blocking)
  void record_stroke(const core::DocumentChangeEvent &stroke_event,
                     const std::vector<CapturedTile> &captured_tiles);

  void record_tile_diff(std::size_t layer_id,
                        const std::vector<CapturedTile> &captured_tiles,
                        std::uint64_t timestamp_ms = 0);

  void record_layer_mutation(core::DocumentChangeType op_type, std::size_t layer_id,
                             const std::string &metadata = "", std::uint64_t timestamp_ms = 0);

  // Synchronous flush - waits until all queued records are written to disk
  void flush_sync();

  bool is_open() const { return is_open_; }
  const std::filesystem::path &journal_path() const { return journal_path_; }
  const std::string &session_uuid() const { return session_uuid_; }

  static std::filesystem::path default_journal_path_for(const std::filesystem::path &project_path,
                                                         const std::string &session_uuid = "");
  static std::uint32_t calculate_crc32(const std::uint8_t *data, std::size_t length);
  static bool uuid_to_bytes(const std::string &uuid_str, std::uint8_t out_bytes[16]);
  static std::string bytes_to_uuid(const std::uint8_t bytes[16]);

private:
  struct PendingRecord {
    WalRecordType type;
    std::size_t layer_id = 0;
    std::uint64_t timestamp_ms = 0;
    core::TrackedBrushSettings brush_settings;
    std::vector<core::StrokeSample> stroke_points;
    std::vector<CapturedTile> tiles;
    core::DocumentChangeType mutation_op = core::DocumentChangeType::LAYER_ADDED;
    std::string metadata;
    std::uint32_t checkpoint_crc = 0;
  };

  void worker_loop();
  void write_record_bytes(WalRecordType type, std::uint64_t timestamp_ms,
                          const std::vector<std::uint8_t> &payload);

  std::filesystem::path journal_path_;
  std::string session_uuid_;
  std::atomic<bool> is_open_{false};
  FILE *file_ = nullptr;

  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::condition_variable flush_cv_;
  std::vector<PendingRecord> queue_front_;
  std::vector<PendingRecord> queue_back_;
  bool stop_worker_ = false;
  std::thread worker_thread_;
};

} // namespace kreart::io
