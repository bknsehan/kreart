#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "kreart/core/document.h"
#include "kreart/core/tiled_image.h"

namespace kreart::io {

struct TimelapseMetadata {
  int version = 1;
  std::string session_uuid;
  int frame_count = 0;
  int fps = 30;
  int width = 0;
  int height = 0;
  double aspect_ratio = 1.0;
};

class TimelapseRecorder {
public:
  TimelapseRecorder();
  ~TimelapseRecorder();

  TimelapseRecorder(const TimelapseRecorder &) = delete;
  TimelapseRecorder &operator=(const TimelapseRecorder &) = delete;

  bool init(const std::string &session_uuid, int max_dimension = 1920);
  void reset(const std::string &session_uuid);
  void stop();

  // Async capture from downscaled GPU readback buffer (non-blocking)
  void capture_frame_async(std::vector<core::Rgba8> pixels, int width, int height, bool force = false);

  // CPU fallback capture (takes snapshot and scales on background worker)
  void capture_document_cpu(const core::Document &document, int max_dimension = 1920, bool force = false);

  // Synchronously flush all pending frame encoding tasks
  void flush_pending_frames();

  // Disk & frame queries
  int frame_count() const { return frame_count_.load(std::memory_order_relaxed); }
  int width() const { return width_; }
  int height() const { return height_; }
  int fps() const { return fps_; }
  const std::filesystem::path &session_dir() const { return session_dir_; }
  const std::string &session_uuid() const { return session_uuid_; }
  std::filesystem::path frame_path_for_index(int frame_index) const;

  void set_fps(int fps) { fps_ = fps; }
  void set_dimensions(int w, int h) { width_ = w; height_ = h; }
  void set_frame_count(int count) { frame_count_.store(count, std::memory_order_relaxed); }

  static std::filesystem::path get_cache_timelapse_dir();
  static std::filesystem::path get_session_dir_for(const std::string &session_uuid);

  static bool save_metadata(const std::filesystem::path &session_dir, const TimelapseMetadata &meta);
  static bool load_metadata(const std::filesystem::path &session_dir, TimelapseMetadata &out_meta);

private:
  struct PendingFrame {
    int frame_index = 0;
    int width = 0;
    int height = 0;
    std::vector<core::Rgba8> pixels;
  };

  void worker_loop();

  std::string session_uuid_;
  std::filesystem::path session_dir_;
  std::atomic<int> frame_count_{0};
  int width_ = 0;
  int height_ = 0;
  int fps_ = 30;
  std::uint64_t last_capture_ms_ = 0;

  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::condition_variable flush_cv_;
  std::vector<PendingFrame> queue_;
  bool stop_worker_ = false;
  std::thread worker_thread_;
};

} // namespace kreart::io
