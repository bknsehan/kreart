#pragma once

#include <cstdint>
#include <string>

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QButtonGroup>
#include <QImage>

#include "kreart/core/tiled_image.h"

namespace kreart::ui {

enum class ColorPickerMode {
  WHEEL,      // HSV Color Wheel (Outer Ring + Inner SV Square)
  RECTANGLE,  // Photoshop / Classic 2D SV Box + Vertical Hue Bar
};

class ColorPickerArea;
class ColorPreviewChip;

class StudioColorPicker : public QWidget {
  Q_OBJECT

public:
  explicit StudioColorPicker(core::Rgba8 initial_color = {24, 24, 28, 255}, QWidget *parent = nullptr);
  ~StudioColorPicker() override = default;

  void set_color(core::Rgba8 color, bool emit_signal = false);
  core::Rgba8 color() const;

  void set_mode(ColorPickerMode mode);
  ColorPickerMode mode() const {
    return mode_;
  }

  // Fast math conversion helpers
  static void rgb_to_hsv(core::Rgba8 rgb, double &h, double &s, double &v);
  static core::Rgba8 hsv_to_rgb(double h, double s, double v, std::uint8_t a = 255);
  static std::string color_to_hex(core::Rgba8 c);
  static bool hex_to_color(const std::string &hex, core::Rgba8 &out_color);

signals:
  void colorChanged(const core::Rgba8 &color);
  void colorCommitted(const core::Rgba8 &color);
private:
  friend class ColorPickerArea;
  friend class ColorPreviewChip;

  void build_ui();
  void on_mode_toggled(ColorPickerMode mode);
  void paint_picker_area(QPainter &p, int width, int height);
  void paint_preview_chip(QPainter &p, int width, int height);

  void handle_mouse_press(double x, double y);
  void handle_mouse_move(double x, double y);
  void handle_mouse_release();

  void update_from_mouse(double x, double y);
  void update_surfaces();
  void update_readouts();

  enum class DragTarget {
    NONE,
    HUE_WHEEL,
    SV_SQUARE,
    HUE_BAR,
    SV_BOX,
  };

  ColorPickerMode mode_ = ColorPickerMode::WHEEL;
  DragTarget drag_target_ = DragTarget::NONE;

  double hue_ = 0.0;        // 0.0 to 360.0
  double saturation_ = 1.0; // 0.0 to 1.0
  double value_ = 1.0;      // 0.0 to 1.0
  std::uint8_t alpha_ = 255;
  bool updating_from_entry_ = false;

  QPushButton *wheel_mode_btn_ = nullptr;
  QPushButton *rect_mode_btn_ = nullptr;
  QButtonGroup *mode_group_ = nullptr;
  ColorPickerArea *picker_area_ = nullptr;
  ColorPreviewChip *preview_chip_ = nullptr;
  QLineEdit *hex_entry_ = nullptr;
  QLabel *rgb_label_ = nullptr;

  QImage wheel_ring_image_;
  QImage sv_square_image_;
  QImage hue_bar_image_;
  QImage sv_box_image_;
  double cached_surface_hue_ = -1.0;
};

} // namespace kreart::ui
