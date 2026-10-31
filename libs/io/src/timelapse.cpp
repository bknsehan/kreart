#include "kreart/io/timelapse.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#include <QImage>
#include <QStandardPaths>
#include <QString>

namespace kreart::io {

namespace {

std::uint64_t current_time_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

std::filesystem::path TimelapseRecorder::get_cache_timelapse_dir() {
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
  std::filesystem::path tl_dir = base / "kreart" / "timelapse";
  std::error_code ec;
  std::filesystem::create_directories(tl_dir, ec);
  return tl_dir;
}

std::filesystem::path TimelapseRecorder::get_session_dir_for(const std::string &session_uuid) {
  std::string uuid = session_uuid.empty() ? "default" : session_uuid;
  std::filesystem::path dir = get_cache_timelapse_dir() / uuid;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

bool TimelapseRecorder::save_metadata(const std::filesystem::path &session_dir,
                                      const TimelapseMetadata &meta) {
  std::filesystem::path meta_path = session_dir / "metadata.json";
  std::ofstream ofs(meta_path);
  if (!ofs.is_open()) return false;

  ofs << "{\n";
  ofs << "  \"version\": " << meta.version << ",\n";
  ofs << "  \"session_uuid\": \"" << meta.session_uuid << "\",\n";
  ofs << "  \"frame_count\": " << meta.frame_count << ",\n";
  ofs << "  \"fps\": " << meta.fps << ",\n";
  ofs << "  \"width\": " << meta.width << ",\n";
  ofs << "  \"height\": " << meta.height << ",\n";
  ofs << "  \"aspect_ratio\": " << meta.aspect_ratio << "\n";
  ofs << "}\n";
  return true;
}

bool TimelapseRecorder::load_metadata(const std::filesystem::path &session_dir,
                                      TimelapseMetadata &out_meta) {
  std::filesystem::path meta_path = session_dir / "metadata.json";
  std::ifstream ifs(meta_path);
  if (!ifs.is_open()) return false;

  std::string line;
  while (std::getline(ifs, line)) {
    auto find_val = [&](const std::string &key) -> std::string {
      auto p = line.find("\"" + key + "\"");
      if (p == std::string::npos) return "";
      auto colon = line.find(':', p);
      if (colon == std::string::npos) return "";
      std::string val = line.substr(colon + 1);
      // Strip whitespace, quotes, commas
      while (!val.empty() && (std::isspace(val.front()) || val.front() == '"')) val.erase(val.begin());
      while (!val.empty() && (std::isspace(val.back()) || val.back() == '"' || val.back() == ',')) val.pop_back();
      return val;
    };

    if (line.find("\"version\"") != std::string::npos) {
      out_meta.version = std::atoi(find_val("version").c_str());
    } else if (line.find("\"session_uuid\"") != std::string::npos) {
      out_meta.session_uuid = find_val("session_uuid");
    } else if (line.find("\"frame_count\"") != std::string::npos) {
      out_meta.frame_count = std::atoi(find_val("frame_count").c_str());
    } else if (line.find("\"fps\"") != std::string::npos) {
      out_meta.fps = std::atoi(find_val("fps").c_str());
    } else if (line.find("\"width\"") != std::string::npos) {
      out_meta.width = std::atoi(find_val("width").c_str());
    } else if (line.find("\"height\"") != std::string::npos) {
      out_meta.height = std::atoi(find_val("height").c_str());
    } else if (line.find("\"aspect_ratio\"") != std::string::npos) {
      out_meta.aspect_ratio = std::atof(find_val("aspect_ratio").c_str());
    }
  }
  return true;
}

std::filesystem::path TimelapseRecorder::frame_path_for_index(int frame_index) const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "frame_%06d.jpg", frame_index);
  return session_dir_ / buf;
}

TimelapseRecorder::TimelapseRecorder() = default;

TimelapseRecorder::~TimelapseRecorder() {
  stop();
}

bool TimelapseRecorder::init(const std::string &session_uuid, int max_dimension) {
  stop();

  session_uuid_ = session_uuid;
  session_dir_ = get_session_dir_for(session_uuid_);
  frame_count_.store(0, std::memory_order_relaxed);
  last_capture_ms_ = 0;

  // Check if existing session metadata exists
  TimelapseMetadata existing;
  if (load_metadata(session_dir_, existing)) {
    frame_count_.store(existing.frame_count, std::memory_order_relaxed);
    width_ = existing.width;
    height_ = existing.height;
    fps_ = existing.fps;
  }

  stop_worker_ = false;
  worker_thread_ = std::thread(&TimelapseRecorder::worker_loop, this);
  return true;
}

void TimelapseRecorder::reset(const std::string &session_uuid) {
  init(session_uuid);
}

void TimelapseRecorder::stop() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    stop_worker_ = true;
  }
  queue_cv_.notify_all();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void TimelapseRecorder::flush_pending_frames() {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  flush_cv_.wait(lock, [this] {
    return queue_.empty();
  });
}

void TimelapseRecorder::capture_frame_async(std::vector<core::Rgba8> pixels, int width, int height,
                                            bool force) {
  if (pixels.empty() || width <= 0 || height <= 0) return;

  const std::uint64_t now_ms = current_time_ms();
  if (!force && (now_ms - last_capture_ms_ < 100)) {
    return; // Rate limited
  }
  last_capture_ms_ = now_ms;

  const int idx = frame_count_.fetch_add(1, std::memory_order_relaxed);
  width_ = width;
  height_ = height;

  PendingFrame frame;
  frame.frame_index = idx;
  frame.width = width;
  frame.height = height;
  frame.pixels = std::move(pixels);

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push_back(std::move(frame));
  }
  queue_cv_.notify_one();
}

void TimelapseRecorder::capture_document_cpu(const core::Document &document, int max_dimension,
                                             bool force) {
  const std::uint64_t now_ms = current_time_ms();
  if (!force && (now_ms - last_capture_ms_ < 100)) {
    return;
  }

  const int doc_w = document.width();
  const int doc_h = document.height();
  if (doc_w <= 0 || doc_h <= 0) return;

  int out_w = doc_w;
  int out_h = doc_h;

  if (max_dimension > 0 && (out_w > max_dimension || out_h > max_dimension)) {
    if (out_w >= out_h) {
      out_w = max_dimension;
      out_h = std::max(1, static_cast<int>(std::round(static_cast<double>(doc_h) * max_dimension / doc_w)));
    } else {
      out_h = max_dimension;
      out_w = std::max(1, static_cast<int>(std::round(static_cast<double>(doc_w) * max_dimension / doc_h)));
    }
  }

  if (out_w > 2 && out_w % 2 != 0) out_w -= 1;
  if (out_h > 2 && out_h % 2 != 0) out_h -= 1;

  // Composite snapshot
  const auto &comp = document.composite_image();
  std::vector<core::Rgba8> pixels(static_cast<std::size_t>(out_w) * out_h);

  for (int y = 0; y < out_h; ++y) {
    const int src_y = static_cast<int>(static_cast<double>(y) * doc_h / out_h);
    for (int x = 0; x < out_w; ++x) {
      const int src_x = static_cast<int>(static_cast<double>(x) * doc_w / out_w);
      pixels[static_cast<std::size_t>(y) * out_w + x] = comp.pixel(src_x, src_y);
    }
  }

  capture_frame_async(std::move(pixels), out_w, out_h, force);
}

void TimelapseRecorder::worker_loop() {
  while (true) {
    PendingFrame item;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] {
        return !queue_.empty() || stop_worker_;
      });

      if (queue_.empty() && stop_worker_) {
        break;
      }

      item = std::move(queue_.front());
      queue_.erase(queue_.begin());
    }

    // Save frame JPEG
    std::filesystem::path frame_path = frame_path_for_index(item.frame_index);
    QImage qimg(reinterpret_cast<const uchar *>(item.pixels.data()),
                item.width, item.height,
                static_cast<qsizetype>(item.width * sizeof(core::Rgba8)),
                QImage::Format_RGBA8888);

    qimg.save(QString::fromStdString(frame_path.string()), "JPG", 85);

    // Periodically update metadata on disk
    TimelapseMetadata meta;
    meta.version = 1;
    meta.session_uuid = session_uuid_;
    meta.frame_count = item.frame_index + 1;
    meta.fps = fps_;
    meta.width = item.width;
    meta.height = item.height;
    meta.aspect_ratio = static_cast<double>(item.width) / item.height;
    save_metadata(session_dir_, meta);

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (queue_.empty()) {
        flush_cv_.notify_all();
      }
    }
  }
}

} // namespace kreart::io
