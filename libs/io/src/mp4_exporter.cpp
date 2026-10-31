#include "kreart/io/mp4_exporter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>

#define MINIMP4_IMPLEMENTATION
#include "video/minimp4.h"
#include "video/minih264.h"

namespace kreart::io {

namespace {

static int mp4_file_write_callback(int64_t offset, const void *buffer, size_t size, void *token) {
  FILE *f = static_cast<FILE *>(token);
  if (!f) return -1;
#if defined(_WIN32)
  if (_fseeki64(f, offset, SEEK_SET) != 0) return -1;
#else
  if (fseeko(f, offset, SEEK_SET) != 0) return -1;
#endif
  return std::fwrite(buffer, 1, size, f) == size ? 0 : -1;
}

void rgb_to_yuv420p(const QImage &img, int enc_w, int enc_h,
                    std::vector<uint8_t> &y_plane,
                    std::vector<uint8_t> &u_plane,
                    std::vector<uint8_t> &v_plane) {
  y_plane.assign(enc_w * enc_h, 16);
  u_plane.assign((enc_w / 2) * (enc_h / 2), 128);
  v_plane.assign((enc_w / 2) * (enc_h / 2), 128);

  const int img_w = img.width();
  const int img_h = img.height();

  for (int y = 0; y < img_h && y < enc_h; ++y) {
    const QRgb *scanline = reinterpret_cast<const QRgb *>(img.constScanLine(y));
    for (int x = 0; x < img_w && x < enc_w; ++x) {
      QRgb px = scanline[x];
      int r = qRed(px);
      int g = qGreen(px);
      int b = qBlue(px);

      // ITU-R BT.601
      int y_val = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
      y_plane[y * enc_w + x] = static_cast<uint8_t>(std::clamp(y_val, 0, 255));

      if ((x % 2 == 0) && (y % 2 == 0)) {
        int u_val = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
        int v_val = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        int uv_idx = (y / 2) * (enc_w / 2) + (x / 2);
        u_plane[uv_idx] = static_cast<uint8_t>(std::clamp(u_val, 0, 255));
        v_plane[uv_idx] = static_cast<uint8_t>(std::clamp(v_val, 0, 255));
      }
    }
  }
}

} // namespace

bool is_ffmpeg_available() {
  return !QStandardPaths::findExecutable("ffmpeg").isEmpty();
}

bool export_timelapse_to_mp4(const std::filesystem::path &frame_dir,
                            int frame_count,
                            const std::filesystem::path &output_mp4,
                            int fps,
                            bool prefer_ffmpeg,
                            std::function<void(int progress_pct)> on_progress,
                            std::string *out_error) {
  if (frame_count <= 0) {
    if (out_error) *out_error = "No frames to export";
    return false;
  }

  if (fps <= 0) fps = 30;

  // 1. Try Hardware / System FFmpeg if available and preferred
  if (prefer_ffmpeg && is_ffmpeg_available()) {
    QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    QProcess process;
    process.setWorkingDirectory(QString::fromStdString(frame_dir.string()));

    QStringList args;
    args << "-y"
         << "-framerate" << QString::number(fps)
         << "-i" << "frame_%06d.jpg"
         << "-c:v" << "libx264"
         << "-pix_fmt" << "yuv420p"
         << QString::fromStdString(output_mp4.string());

    process.start(ffmpeg, args);

    if (process.waitForStarted(5000)) {
      QRegularExpression frame_rx("frame=\\s*(\\d+)");
      while (process.state() == QProcess::Running) {
        if (process.waitForReadyRead(500)) {
          QString output = QString::fromUtf8(process.readAllStandardError());
          auto match = frame_rx.match(output);
          if (match.hasMatch() && on_progress) {
            int current_frame = match.captured(1).toInt();
            int pct = std::clamp((current_frame * 100) / frame_count, 0, 99);
            on_progress(pct);
          }
        }
      }
      process.waitForFinished(30000);
      if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        std::error_code ec;
        if (std::filesystem::exists(output_mp4, ec) && std::filesystem::file_size(output_mp4, ec) > 0) {
          if (on_progress) on_progress(100);
          return true;
        }
      }
    }
  }

  // 2. Embedded Universal Fallback H.264 Encoder
  char first_frame_path[32];
  std::snprintf(first_frame_path, sizeof(first_frame_path), "frame_%06d.jpg", 0);
  std::filesystem::path f0 = frame_dir / first_frame_path;
  QImage sample_img(QString::fromStdString(f0.string()));
  if (sample_img.isNull()) {
    if (out_error) *out_error = "Could not read initial timelapse frame";
    return false;
  }

  int raw_w = sample_img.width();
  int raw_h = sample_img.height();
  int enc_w = (raw_w + 15) & ~15; // Multiple of 16
  int enc_h = (raw_h + 15) & ~15;
  H264E_create_param_t enc_param{};
  enc_param.width = enc_w;
  enc_param.height = enc_h;
  enc_param.gop = std::min(fps * 2, 60);
  enc_param.const_input_flag = 1;
  enc_param.max_threads = 1;

  int sizeof_persist = 0, sizeof_scratch = 0;
  if (H264E_sizeof(&enc_param, &sizeof_persist, &sizeof_scratch) != H264E_STATUS_SUCCESS) {
    if (out_error) *out_error = "Failed to calculate H264 encoder requirements";
    return false;
  }
  std::vector<uint8_t> persist_buf(sizeof_persist);
  std::vector<uint8_t> scratch_buf(sizeof_scratch);

  auto *enc = reinterpret_cast<H264E_persist_t *>(persist_buf.data());
  auto *scratch = reinterpret_cast<H264E_scratch_t *>(scratch_buf.data());
  if (H264E_init(enc, &enc_param) != H264E_STATUS_SUCCESS) {
    if (out_error) *out_error = "Failed to initialize H264 encoder";
    return false;
  }

  FILE *mp4_file = std::fopen(output_mp4.string().c_str(), "wb+");
  if (!mp4_file) {
    if (out_error) *out_error = "Failed to open output MP4 file for writing";
    return false;
  }

  MP4E_mux_t *mux = MP4E_open(0, 0, mp4_file, mp4_file_write_callback);
  if (!mux) {
    std::fclose(mp4_file);
    if (out_error) *out_error = "Failed to initialize MP4 multiplexer";
    return false;
  }

  mp4_h26x_writer_t mp4_writer{};
  if (mp4_h26x_write_init(&mp4_writer, mux, enc_w, enc_h, 0) != MP4E_STATUS_OK) {
    MP4E_close(mux);
    std::fclose(mp4_file);
    if (out_error) *out_error = "Failed to initialize MP4 video track";
    return false;
  }

  std::vector<uint8_t> y_plane;
  std::vector<uint8_t> u_plane;
  std::vector<uint8_t> v_plane;
  const uint32_t step90k = 90000 / fps;
  uint32_t timestamp90k = 0;

  for (int i = 0; i < frame_count; ++i) {
    char frame_name[32];
    std::snprintf(frame_name, sizeof(frame_name), "frame_%06d.jpg", i);
    std::filesystem::path fp = frame_dir / frame_name;

    QImage img(QString::fromStdString(fp.string()));
    if (img.isNull()) continue;
    img = img.convertToFormat(QImage::Format_ARGB32);

    rgb_to_yuv420p(img, enc_w, enc_h, y_plane, u_plane, v_plane);

    H264E_io_yuv_t yuv{};
    yuv.yuv[0] = y_plane.data();
    yuv.yuv[1] = u_plane.data();
    yuv.yuv[2] = v_plane.data();
    yuv.stride[0] = enc_w;
    yuv.stride[1] = enc_w / 2;
    yuv.stride[2] = enc_w / 2;
    H264E_run_param_t run_param{};
    run_param.encode_speed = 0;
    run_param.qp_min = 18;
    run_param.qp_max = 32;

    unsigned char *coded_data = nullptr;
    int sizeof_coded_data = 0;
    int encode_status = H264E_encode(enc, scratch, &run_param, &yuv, &coded_data, &sizeof_coded_data);
    if (encode_status == H264E_STATUS_SUCCESS && sizeof_coded_data > 0) {
      mp4_h26x_write_nal(&mp4_writer, coded_data, sizeof_coded_data, timestamp90k);
    }

    timestamp90k += step90k;

    if (on_progress) {
      on_progress(((i + 1) * 100) / frame_count);
    }
  }

  mp4_h26x_write_close(&mp4_writer);
  MP4E_close(mux);
  std::fclose(mp4_file);

  std::error_code ec;
  if (!std::filesystem::exists(output_mp4, ec) || std::filesystem::file_size(output_mp4, ec) == 0) {
    if (out_error) *out_error = "Encoder produced empty output file";
    return false;
  }

  if (on_progress) on_progress(100);
  return true;
}

} // namespace kreart::io
