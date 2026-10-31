#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace kreart::io {

// Returns true if system ffmpeg executable is found
bool is_ffmpeg_available();

// Exports sequential JPEG frames (frame_%06d.jpg) to universal H.264 MP4.
// If prefer_ffmpeg is true and ffmpeg is installed, uses hardware ffmpeg process.
// Otherwise encodes using embedded H.264 engine (minih264 + minimp4).
// on_progress: callback reporting progress 0..100%.
bool export_timelapse_to_mp4(const std::filesystem::path &frame_dir,
                            int frame_count,
                            const std::filesystem::path &output_mp4,
                            int fps = 30,
                            bool prefer_ffmpeg = true,
                            std::function<void(int progress_pct)> on_progress = nullptr,
                            std::string *out_error = nullptr);

} // namespace kreart::io
