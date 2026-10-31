#include "kreart/ui/timelapse_dialog.h"
#include "kreart/io/mp4_exporter.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QVBoxLayout>

namespace kreart::ui {

TimelapseDialog::TimelapseDialog(const io::TimelapseRecorder &recorder,
                                 const core::ProjectTracker &tracker,
                                 QWidget *parent)
    : QDialog(parent), recorder_(recorder), tracker_(tracker) {
  setWindowTitle(tr("Timelapse Replay & Export"));
  resize(850, 650);

  total_frames_ = recorder_.frame_count();

  auto *main_layout = new QVBoxLayout(this);

  // 1. Central Image Preview
  preview_label_ = new QLabel(this);
  preview_label_->setAlignment(Qt::AlignCenter);
  preview_label_->setMinimumSize(480, 320);
  preview_label_->setStyleSheet("background-color: #1a1a1a; border-radius: 6px;");
  main_layout->addWidget(preview_label_, 1);

  // 2. Scrubber Controls
  auto *scrub_layout = new QHBoxLayout();
  play_pause_btn_ = new QPushButton(tr("Play"), this);
  play_pause_btn_->setFixedWidth(80);
  scrub_layout->addWidget(play_pause_btn_);

  scrubber_slider_ = new QSlider(Qt::Horizontal, this);
  scrubber_slider_->setRange(0, std::max(0, total_frames_ - 1));
  scrubber_slider_->setValue(0);
  scrub_layout->addWidget(scrubber_slider_, 1);

  frame_info_label_ = new QLabel(tr("Frame 0 / %1").arg(total_frames_), this);
  frame_info_label_->setFixedWidth(110);
  scrub_layout->addWidget(frame_info_label_);

  speed_combo_ = new QComboBox(this);
  speed_combo_->addItems({"1x", "2x", "4x", "8x"});
  scrub_layout->addWidget(speed_combo_);

  main_layout->addLayout(scrub_layout);

  // 3. Stats & Canvas Information
  auto *stats_group = new QGroupBox(tr("Canvas Statistics & Timelapse Info"), this);
  auto *stats_box_layout = new QVBoxLayout(stats_group);

  const std::uint64_t draw_time_sec = tracker_.active_draw_time_ms() / 1000;
  const unsigned int hours = static_cast<unsigned int>(draw_time_sec / 3600);
  const unsigned int minutes = static_cast<unsigned int>((draw_time_sec % 3600) / 60);
  const unsigned int seconds = static_cast<unsigned int>(draw_time_sec % 60);

  const double video_duration_sec = total_frames_ > 0 ? (total_frames_ / 30.0) : 0.0;

  QString stats_text = tr("Total Strokes: %1 | Tracked Drawing Time: %2h %3m %4s | Recorded Frames: %5 (%6s at 30 fps)")
                           .arg(tracker_.total_strokes())
                           .arg(hours)
                           .arg(minutes)
                           .arg(seconds)
                           .arg(total_frames_)
                           .arg(QString::number(video_duration_sec, 'f', 1));

  stats_label_ = new QLabel(stats_text, stats_group);
  stats_box_layout->addWidget(stats_label_);
  main_layout->addWidget(stats_group);

  // 4. Export Controls
  auto *export_layout = new QHBoxLayout();
  export_progress_ = new QProgressBar(this);
  export_progress_->setRange(0, 100);
  export_progress_->setValue(0);
  export_progress_->setVisible(false);
  export_layout->addWidget(export_progress_, 1);

  export_btn_ = new QPushButton(tr("Export Video (MP4)..."), this);
  export_btn_->setEnabled(total_frames_ > 0);
  export_layout->addWidget(export_btn_);

  auto *close_btn = new QPushButton(tr("Close"), this);
  export_layout->addWidget(close_btn);

  main_layout->addLayout(export_layout);

  // Connections
  connect(scrubber_slider_, &QSlider::valueChanged, this, &TimelapseDialog::on_slider_changed);
  connect(play_pause_btn_, &QPushButton::clicked, this, &TimelapseDialog::on_play_pause_clicked);
  connect(&playback_timer_, &QTimer::timeout, this, &TimelapseDialog::on_playback_timer_tick);
  connect(speed_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TimelapseDialog::on_speed_changed);
  connect(export_btn_, &QPushButton::clicked, this, &TimelapseDialog::on_export_clicked);
  connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

  if (total_frames_ > 0) {
    show_frame(0);
  }
}

TimelapseDialog::~TimelapseDialog() {
  playback_timer_.stop();
}

void TimelapseDialog::show_frame(int index) {
  if (total_frames_ <= 0) return;
  current_frame_ = std::clamp(index, 0, total_frames_ - 1);

  std::filesystem::path fp = recorder_.frame_path_for_index(current_frame_);
  QPixmap pix(QString::fromStdString(fp.string()));
  if (!pix.isNull()) {
    preview_label_->setPixmap(pix.scaled(preview_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  frame_info_label_->setText(tr("Frame %1 / %2").arg(current_frame_ + 1).arg(total_frames_));
}

void TimelapseDialog::on_slider_changed(int value) {
  show_frame(value);
}

void TimelapseDialog::on_play_pause_clicked() {
  if (total_frames_ <= 1) return;

  is_playing_ = !is_playing_;
  update_play_pause_button();

  if (is_playing_) {
    int interval_ms = std::max(10, static_cast<int>(1000.0 / (30.0 * playback_speed_)));
    playback_timer_.start(interval_ms);
  } else {
    playback_timer_.stop();
  }
}

void TimelapseDialog::update_play_pause_button() {
  play_pause_btn_->setText(is_playing_ ? tr("Pause") : tr("Play"));
}

void TimelapseDialog::on_playback_timer_tick() {
  if (total_frames_ <= 0) return;
  current_frame_ = (current_frame_ + 1) % total_frames_;
  scrubber_slider_->setValue(current_frame_);
}

void TimelapseDialog::on_speed_changed(int index) {
  switch (index) {
    case 0: playback_speed_ = 1.0; break;
    case 1: playback_speed_ = 2.0; break;
    case 2: playback_speed_ = 4.0; break;
    case 3: playback_speed_ = 8.0; break;
    default: playback_speed_ = 1.0; break;
  }
  if (is_playing_) {
    int interval_ms = std::max(10, static_cast<int>(1000.0 / (30.0 * playback_speed_)));
    playback_timer_.setInterval(interval_ms);
  }
}

void TimelapseDialog::on_export_clicked() {
  if (total_frames_ <= 0) return;

  const QString save_path = QFileDialog::getSaveFileName(
      this, tr("Export Timelapse Video"), QString(), tr("MP4 Video (*.mp4)"));
  if (save_path.isEmpty()) return;

  std::filesystem::path out_path = save_path.toStdString();
  if (out_path.extension() != ".mp4") {
    out_path += ".mp4";
  }

  export_progress_->setValue(0);
  export_progress_->setVisible(true);
  export_btn_->setEnabled(false);

  // Run export
  std::string err;
  bool ok = io::export_timelapse_to_mp4(
      recorder_.session_dir(), total_frames_, out_path, 30, true,
      [this](int pct) {
        export_progress_->setValue(pct);
        QApplication::processEvents();
      },
      &err);

  export_progress_->setVisible(false);
  export_btn_->setEnabled(true);

  if (ok) {
    QMessageBox::information(this, tr("Export Complete"),
                             tr("Timelapse video successfully exported to:\n%1")
                                 .arg(QString::fromStdString(out_path.string())));
  } else {
    QMessageBox::critical(this, tr("Export Failed"),
                          tr("Failed to export timelapse video:\n%1")
                              .arg(QString::fromStdString(err)));
  }
}

} // namespace kreart::ui
