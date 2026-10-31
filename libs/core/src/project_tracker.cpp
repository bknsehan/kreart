#include "kreart/core/project_tracker.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace kreart::core {

ProjectTracker::ProjectTracker() {
  active_stroke_.stroke_points.reserve(512);
}

ProjectTracker::~ProjectTracker() = default;

std::string ProjectTracker::generate_uuid() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<std::uint64_t> dis;

  std::uint64_t p1 = dis(gen);
  std::uint64_t p2 = dis(gen);

  // Set version 4 (random) and variant (RFC 4122)
  p1 = (p1 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  p2 = (p2 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  oss << std::setw(8) << static_cast<std::uint32_t>(p1 >> 32) << "-";
  oss << std::setw(4) << static_cast<std::uint16_t>((p1 >> 16) & 0xFFFF) << "-";
  oss << std::setw(4) << static_cast<std::uint16_t>(p1 & 0xFFFF) << "-";
  oss << std::setw(4) << static_cast<std::uint16_t>(p2 >> 48) << "-";
  oss << std::setw(12) << (p2 & 0xFFFFFFFFFFFFULL);
  return oss.str();
}

std::uint64_t ProjectTracker::current_timestamp_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

void ProjectTracker::reset(int width, int height, const std::string &session_uuid) {
  doc_width_ = width;
  doc_height_ = height;
  session_uuid_ = session_uuid.empty() ? generate_uuid() : session_uuid;
  in_stroke_ = false;
  total_strokes_ = 0;
  active_draw_time_ms_ = 0;
  stroke_start_ms_ = 0;
  active_stroke_.stroke_points.clear();
  active_stroke_.stroke_points.reserve(512);
  active_stroke_.modified_tile_keys.clear();
}

void ProjectTracker::begin_stroke(std::size_t layer_id,
                                  const TrackedBrushSettings &settings,
                                  std::uint64_t timestamp_ms) {
  if (timestamp_ms == 0) {
    timestamp_ms = current_timestamp_ms();
  }
  in_stroke_ = true;
  stroke_start_ms_ = timestamp_ms;
  active_stroke_.type = DocumentChangeType::STROKE_COMMITTED;
  active_stroke_.layer_id = layer_id;
  active_stroke_.timestamp_ms = timestamp_ms;
  active_stroke_.brush_settings = settings;
  active_stroke_.stroke_points.clear();
  active_stroke_.stroke_points.reserve(512);
  active_stroke_.modified_tile_keys.clear();
}

void ProjectTracker::append_sample(const StrokeSample &sample) {
  if (!in_stroke_) {
    return;
  }
  active_stroke_.stroke_points.push_back(sample);
}

void ProjectTracker::commit_stroke(const std::vector<std::uint64_t> &modified_tile_keys,
                                   std::uint64_t timestamp_ms) {
  if (!in_stroke_) {
    return;
  }
  if (timestamp_ms == 0) {
    timestamp_ms = current_timestamp_ms();
  }
  if (timestamp_ms >= stroke_start_ms_) {
    active_draw_time_ms_ += (timestamp_ms - stroke_start_ms_);
  }
  ++total_strokes_;
  active_stroke_.modified_tile_keys = modified_tile_keys;

  if (listener_) {
    listener_(active_stroke_);
  }

  in_stroke_ = false;
  active_stroke_.stroke_points.clear();
  active_stroke_.modified_tile_keys.clear();
}

void ProjectTracker::cancel_stroke() {
  in_stroke_ = false;
  active_stroke_.stroke_points.clear();
  active_stroke_.modified_tile_keys.clear();
}

void ProjectTracker::record_document_change(DocumentChangeType type,
                                           std::size_t layer_id,
                                           const std::vector<std::uint64_t> &modified_tile_keys,
                                           std::uint64_t timestamp_ms) {
  if (timestamp_ms == 0) {
    timestamp_ms = current_timestamp_ms();
  }
  DocumentChangeEvent event;
  event.type = type;
  event.layer_id = layer_id;
  event.timestamp_ms = timestamp_ms;
  event.modified_tile_keys = modified_tile_keys;

  if (listener_) {
    listener_(event);
  }
}

void ProjectTracker::set_change_listener(ChangeListener listener) {
  listener_ = std::move(listener);
}

} // namespace kreart::core
