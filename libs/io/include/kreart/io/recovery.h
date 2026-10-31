#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "kreart/core/document.h"

namespace kreart::io {

struct WalRecoveryStats {
  std::uint64_t total_records_processed = 0;
  std::uint64_t stroke_events_replayed = 0;
  std::uint64_t tile_diffs_replayed = 0;
  std::uint64_t mutations_applied = 0;
  bool reached_clean_checkpoint = false;
  bool reached_eof_or_corrupt_tail = false;
  std::string session_uuid;
  int doc_width = 0;
  int doc_height = 0;
};

// Replays a WAL file into the given document.
// Returns true if at least one record was successfully replayed.
bool recover_document_from_wal(const std::filesystem::path &wal_path,
                               core::Document &document,
                               std::string &out_report,
                               WalRecoveryStats *out_stats = nullptr);

// Inspects a WAL file without modifying any document.
bool inspect_wal(const std::filesystem::path &wal_path, WalRecoveryStats &stats, std::string &error);

// Checks if a companion or cached journal exists for a document path.
bool has_uncommitted_wal(const std::filesystem::path &project_path, std::filesystem::path &out_wal_path);

} // namespace kreart::io
