#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

#include "kreart/core/project_tracker.h"
#include "kreart/io/timelapse.h"

namespace kreart::ui {

class TimelapseDialog : public QDialog {
  Q_OBJECT

public:
  explicit TimelapseDialog(const io::TimelapseRecorder &recorder,
                           const core::ProjectTracker &tracker,
                           QWidget *parent = nullptr);
  ~TimelapseDialog() override;

private slots:
  void on_slider_changed(int value);
  void on_play_pause_clicked();
  void on_playback_timer_tick();
  void on_speed_changed(int index);
  void on_export_clicked();

private:
  void show_frame(int index);
  void update_play_pause_button();

  const io::TimelapseRecorder &recorder_;
  const core::ProjectTracker &tracker_;

  QLabel *preview_label_ = nullptr;
  QSlider *scrubber_slider_ = nullptr;
  QLabel *frame_info_label_ = nullptr;
  QPushButton *play_pause_btn_ = nullptr;
  QComboBox *speed_combo_ = nullptr;
  QPushButton *export_btn_ = nullptr;
  QProgressBar *export_progress_ = nullptr;
  QLabel *stats_label_ = nullptr;

  QTimer playback_timer_;
  int current_frame_ = 0;
  int total_frames_ = 0;
  bool is_playing_ = false;
  double playback_speed_ = 1.0;
};

} // namespace kreart::ui
