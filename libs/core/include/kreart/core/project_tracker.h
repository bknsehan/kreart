#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "kreart/core/tracker_events.h"

namespace kreart::core {

class ProjectTracker {
public:
  ProjectTracker();
  ~ProjectTracker();

  void reset(int width, int height, const std::string &session_uuid = "");
  void begin_stroke(std::size_t layer_id, const TrackedBrushSettings &settings, std::uint64_t timestamp_ms = 0);
  void append_sample(const StrokeSample &sample);
  void commit_stroke(const std::vector<std::uint64_t> &modified_tile_keys, std::uint64_t timestamp_ms = 0);
  void cancel_stroke();

  void record_document_change(DocumentChangeType type, std::size_t layer_id,
                              const std::vector<std::uint64_t> &modified_tile_keys = {},
                              std::uint64_t timestamp_ms = 0);

  using ChangeListener = std::function<void(const DocumentChangeEvent &)>;
  void set_change_listener(ChangeListener listener);

  bool in_stroke() const { return in_stroke_; }
  std::uint64_t total_strokes() const { return total_strokes_; }
  std::uint64_t active_draw_time_ms() const { return active_draw_time_ms_; }
  const std::string &session_uuid() const { return session_uuid_; }
  int doc_width() const { return doc_width_; }
  int doc_height() const { return doc_height_; }

  void set_stats(std::uint64_t total_strokes, std::uint64_t active_draw_time_ms) {
    total_strokes_ = total_strokes;
    active_draw_time_ms_ = active_draw_time_ms;
  }

  static std::string generate_uuid();
  static std::uint64_t current_timestamp_ms();

private:
  int doc_width_ = 0;
  int doc_height_ = 0;
  std::string session_uuid_;
  bool in_stroke_ = false;
  DocumentChangeEvent active_stroke_;
  std::uint64_t total_strokes_ = 0;
  std::uint64_t active_draw_time_ms_ = 0;
  std::uint64_t stroke_start_ms_ = 0;
  ChangeListener listener_;
};

} // namespace kreart::core
