#include "kreart/ui/color_picker.h"

#include <QBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace kreart::ui {

class ColorPickerArea : public QWidget {
public:
  explicit ColorPickerArea(StudioColorPicker *picker, QWidget *parent = nullptr)
      : QWidget(parent), picker_(picker) {
    setFixedSize(210, 200);
    setCursor(Qt::CrossCursor);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    picker_->paint_picker_area(p, width(), height());
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      picker_->handle_mouse_press(event->position().x(), event->position().y());
    }
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    picker_->handle_mouse_move(event->position().x(), event->position().y());
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      picker_->handle_mouse_release();
    }
  }

private:
  StudioColorPicker *picker_ = nullptr;
};

class ColorPreviewChip : public QWidget {
public:
  explicit ColorPreviewChip(StudioColorPicker *picker, QWidget *parent = nullptr)
      : QWidget(parent), picker_(picker) {
    setFixedSize(32, 32);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    picker_->paint_preview_chip(p, width(), height());
  }

private:
  StudioColorPicker *picker_ = nullptr;
};

StudioColorPicker::StudioColorPicker(core::Rgba8 initial_color, QWidget *parent)
    : QWidget(parent) {
  rgb_to_hsv(initial_color, hue_, saturation_, value_);
  alpha_ = initial_color.a;

  build_ui();
  update_surfaces();
  update_readouts();
}

void StudioColorPicker::rgb_to_hsv(core::Rgba8 rgb, double &h, double &s, double &v) {
  const double r = rgb.r / 255.0;
  const double g = rgb.g / 255.0;
  const double b = rgb.b / 255.0;
  const double c_max = std::max({r, g, b});
  const double c_min = std::min({r, g, b});
  const double delta = c_max - c_min;

  if (delta < 1e-6) {
    h = 0.0;
  } else if (c_max == r) {
    h = 60.0 * std::fmod((g - b) / delta, 6.0);
  } else if (c_max == g) {
    h = 60.0 * (((b - r) / delta) + 2.0);
  } else {
    h = 60.0 * (((r - g) / delta) + 4.0);
  }
  if (h < 0.0) {
    h += 360.0;
  }

  s = (c_max < 1e-6) ? 0.0 : (delta / c_max);
  v = c_max;
}

core::Rgba8 StudioColorPicker::hsv_to_rgb(double h, double s, double v, std::uint8_t a) {
  h = std::clamp(h, 0.0, 360.0);
  if (h >= 360.0) h = 0.0;
  s = std::clamp(s, 0.0, 1.0);
  v = std::clamp(v, 0.0, 1.0);

  const double c = v * s;
  const double x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
  const double m = v - c;

  double r_f = 0.0, g_f = 0.0, b_f = 0.0;
  if (h < 60.0) {
    r_f = c; g_f = x; b_f = 0.0;
  } else if (h < 120.0) {
    r_f = x; g_f = c; b_f = 0.0;
  } else if (h < 180.0) {
    r_f = 0.0; g_f = c; b_f = x;
  } else if (h < 240.0) {
    r_f = 0.0; g_f = x; b_f = c;
  } else if (h < 300.0) {
    r_f = x; g_f = 0.0; b_f = c;
  } else {
    r_f = c; g_f = 0.0; b_f = x;
  }

  const int r = std::clamp(static_cast<int>(std::round((r_f + m) * 255.0)), 0, 255);
  const int g = std::clamp(static_cast<int>(std::round((g_f + m) * 255.0)), 0, 255);
  const int b = std::clamp(static_cast<int>(std::round((b_f + m) * 255.0)), 0, 255);
  return core::Rgba8{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                     static_cast<std::uint8_t>(b), a};
}

std::string StudioColorPicker::color_to_hex(core::Rgba8 c) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
  return std::string(buf);
}

bool StudioColorPicker::hex_to_color(const std::string &hex, core::Rgba8 &out_color) {
  std::string s = hex;
  if (!s.empty() && s[0] == '#') {
    s = s.substr(1);
  }
  if (s.size() != 6 && s.size() != 8) {
    return false;
  }
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  unsigned int r = 0, g = 0, b = 0, a = 255;
  if (s.size() == 6) {
    if (std::sscanf(s.c_str(), "%02x%02x%02x", &r, &g, &b) != 3) {
      return false;
    }
  } else if (s.size() == 8) {
    if (std::sscanf(s.c_str(), "%02x%02x%02x%02x", &r, &g, &b, &a) != 4) {
      return false;
    }
  }
  out_color = core::Rgba8{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                          static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
  return true;
}

core::Rgba8 StudioColorPicker::color() const {
  return hsv_to_rgb(hue_, saturation_, value_, alpha_);
}

void StudioColorPicker::set_color(core::Rgba8 color, bool emit_signal) {
  rgb_to_hsv(color, hue_, saturation_, value_);
  alpha_ = color.a;
  update_surfaces();
  update_readouts();
  if (picker_area_) picker_area_->update();
  if (preview_chip_) preview_chip_->update();
  if (emit_signal) {
    emit colorChanged(color);
  }
}

void StudioColorPicker::set_mode(ColorPickerMode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  if (wheel_mode_btn_) wheel_mode_btn_->setChecked(mode_ == ColorPickerMode::WHEEL);
  if (rect_mode_btn_) rect_mode_btn_->setChecked(mode_ == ColorPickerMode::RECTANGLE);
  update_surfaces();
  if (picker_area_) picker_area_->update();
}

void StudioColorPicker::on_mode_toggled(ColorPickerMode mode) {
  set_mode(mode);
}

void StudioColorPicker::build_ui() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  // Top Mode Switcher
  auto *mode_layout = new QHBoxLayout();
  mode_layout->setSpacing(2);

  mode_group_ = new QButtonGroup(this);
  const QString mode_btn_css = QString(
      "QPushButton { border-radius: 6px; background-color: rgba(255, 255, 255, 0.08); color: #aaa; padding: 4px 12px; font-weight: bold; } "
      "QPushButton:checked { background-color: %1; color: %2; }")
      .arg(palette().color(QPalette::Highlight).name(), palette().color(QPalette::HighlightedText).name());

  wheel_mode_btn_ = new QPushButton(tr("Wheel"), this);
  wheel_mode_btn_->setCheckable(true);
  wheel_mode_btn_->setChecked(mode_ == ColorPickerMode::WHEEL);
  wheel_mode_btn_->setStyleSheet(mode_btn_css);
  mode_group_->addButton(wheel_mode_btn_);
  mode_layout->addWidget(wheel_mode_btn_);

  rect_mode_btn_ = new QPushButton(tr("Square"), this);
  rect_mode_btn_->setCheckable(true);
  rect_mode_btn_->setChecked(mode_ == ColorPickerMode::RECTANGLE);
  rect_mode_btn_->setStyleSheet(mode_btn_css);
  mode_group_->addButton(rect_mode_btn_);
  mode_layout->addWidget(rect_mode_btn_);

  connect(wheel_mode_btn_, &QPushButton::clicked, this, [this]() {
    on_mode_toggled(ColorPickerMode::WHEEL);
  });
  connect(rect_mode_btn_, &QPushButton::clicked, this, [this]() {
    on_mode_toggled(ColorPickerMode::RECTANGLE);
  });

  layout->addLayout(mode_layout);

  // Center: Picker Area
  picker_area_ = new ColorPickerArea(this, this);
  layout->addWidget(picker_area_, 0, Qt::AlignCenter);

  // Bottom row: Preview chip & Hex entry
  auto *bottom_layout = new QHBoxLayout();
  bottom_layout->setSpacing(8);

  preview_chip_ = new ColorPreviewChip(this, this);
  bottom_layout->addWidget(preview_chip_);

  hex_entry_ = new QLineEdit(this);
  hex_entry_->setPlaceholderText("#RRGGBB");
  hex_entry_->setMaxLength(9);
  hex_entry_->setStyleSheet(
      "background-color: rgba(255, 255, 255, 0.08); color: #fff; border-radius: 6px; padding: 4px 8px; border: 1px solid rgba(255, 255, 255, 0.15);");
  connect(hex_entry_, &QLineEdit::textChanged, this, [this](const QString &text) {
    if (updating_from_entry_) return;
    core::Rgba8 parsed;
    if (hex_to_color(text.toStdString(), parsed)) {
      updating_from_entry_ = true;
      set_color(parsed, true);
      updating_from_entry_ = false;
    }
  });
  connect(hex_entry_, &QLineEdit::returnPressed, this, [this]() {
    emit colorCommitted(color());
  });
  connect(hex_entry_, &QLineEdit::editingFinished, this, [this]() {
    emit colorCommitted(color());
  });
  bottom_layout->addWidget(hex_entry_);

  layout->addLayout(bottom_layout);

  // RGB numerical readout label
  rgb_label_ = new QLabel(this);
  rgb_label_->setStyleSheet("font-size: 11px;");
  layout->addWidget(rgb_label_);
}

void StudioColorPicker::update_surfaces() {
  if (std::abs(cached_surface_hue_ - hue_) < 0.2) {
    return;
  }
  cached_surface_hue_ = hue_;

  // 1. Color Wheel Ring surface (cached once)
  if (wheel_ring_image_.isNull()) {
    wheel_ring_image_ = QImage(210, 200, QImage::Format_ARGB32_Premultiplied);
    wheel_ring_image_.fill(Qt::transparent);

    const double cx = 105.0;
    const double cy = 100.0;
    const double r_out = 92.0;
    const double r_in = 74.0;

    for (int y = 0; y < 200; ++y) {
      auto *line = reinterpret_cast<QRgb *>(wheel_ring_image_.scanLine(y));
      for (int x = 0; x < 210; ++x) {
        const double dx = x - cx;
        const double dy = y - cy;
        const double d = std::hypot(dx, dy);
        if (d >= r_in && d <= r_out) {
          double ang = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
          if (ang < 0.0) ang += 360.0;
          const auto rgb = hsv_to_rgb(ang, 1.0, 1.0);
          line[x] = qRgb(rgb.r, rgb.g, rgb.b);
        } else {
          line[x] = 0;
        }
      }
    }
  }

  // 2. SV Square image for Wheel Mode (96x96 pixels)
  {
    constexpr int kSqSize = 96;
    sv_square_image_ = QImage(kSqSize, kSqSize, QImage::Format_RGB32);
    for (int y = 0; y < kSqSize; ++y) {
      const double v = 1.0 - static_cast<double>(y) / (kSqSize - 1);
      auto *line = reinterpret_cast<QRgb *>(sv_square_image_.scanLine(y));
      for (int x = 0; x < kSqSize; ++x) {
        const double s = static_cast<double>(x) / (kSqSize - 1);
        const auto rgb = hsv_to_rgb(hue_, s, v);
        line[x] = qRgb(rgb.r, rgb.g, rgb.b);
      }
    }
  }

  // 3. Vertical Hue Bar image for Rectangle Mode (24x180 pixels, cached once)
  if (hue_bar_image_.isNull()) {
    constexpr int kBarW = 24;
    constexpr int kBarH = 180;
    hue_bar_image_ = QImage(kBarW, kBarH, QImage::Format_RGB32);
    for (int y = 0; y < kBarH; ++y) {
      const double h = (static_cast<double>(y) / (kBarH - 1)) * 360.0;
      const auto rgb = hsv_to_rgb(h, 1.0, 1.0);
      const QRgb px = qRgb(rgb.r, rgb.g, rgb.b);
      auto *line = reinterpret_cast<QRgb *>(hue_bar_image_.scanLine(y));
      for (int x = 0; x < kBarW; ++x) {
        line[x] = px;
      }
    }
  }

  // 4. SV Box image for Rectangle Mode (155x180 pixels)
  {
    constexpr int kBoxW = 155;
    constexpr int kBoxH = 180;
    sv_box_image_ = QImage(kBoxW, kBoxH, QImage::Format_RGB32);
    for (int y = 0; y < kBoxH; ++y) {
      const double v = 1.0 - static_cast<double>(y) / (kBoxH - 1);
      auto *line = reinterpret_cast<QRgb *>(sv_box_image_.scanLine(y));
      for (int x = 0; x < kBoxW; ++x) {
        const double s = static_cast<double>(x) / (kBoxW - 1);
        const auto rgb = hsv_to_rgb(hue_, s, v);
        line[x] = qRgb(rgb.r, rgb.g, rgb.b);
      }
    }
  }
}

void StudioColorPicker::update_readouts() {
  const auto c = color();
  if (hex_entry_ && !updating_from_entry_) {
    updating_from_entry_ = true;
    hex_entry_->setText(QString::fromStdString(color_to_hex(c)));
    updating_from_entry_ = false;
  }
  if (rgb_label_) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "R:%d  G:%d  B:%d  (H:%.0f° S:%.0f%% V:%.0f%%)",
                  c.r, c.g, c.b, hue_, saturation_ * 100.0, value_ * 100.0);
    rgb_label_->setText(QString::fromUtf8(buf));
  }
}

void StudioColorPicker::paint_picker_area(QPainter &p, int /*width*/, int /*height*/) {
  update_surfaces();

  if (mode_ == ColorPickerMode::WHEEL) {
    if (!wheel_ring_image_.isNull()) {
      p.drawImage(0, 0, wheel_ring_image_);
    }

    if (!sv_square_image_.isNull()) {
      p.drawImage(57, 52, sv_square_image_);
      p.setPen(QPen(QColor(255, 255, 255, 64), 1.0));
      p.drawRect(QRectF(56.5, 51.5, 97.0, 97.0));
    }

    // Hue indicator
    const double rad = hue_ * std::numbers::pi / 180.0;
    const double ring_r = (92.0 + 74.0) / 2.0;
    const double hx = 105.0 + ring_r * std::cos(rad);
    const double hy = 100.0 + ring_r * std::sin(rad);

    p.setPen(QPen(QColor(25, 25, 38), 1.5));
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(hx, hy), 5.0, 5.0);

    // SV indicator
    const double sx = 57.0 + saturation_ * 96.0;
    const double sy = 52.0 + (1.0 - value_) * 96.0;

    p.setPen(QPen(Qt::black, 2.0));
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(sx, sy), 4.5, 4.5);
  } else {
    if (!sv_box_image_.isNull()) {
      p.drawImage(10, 10, sv_box_image_);
      p.setPen(QPen(QColor(255, 255, 255, 64), 1.0));
      p.drawRect(QRectF(9.5, 9.5, 156.0, 181.0));
    }

    if (!hue_bar_image_.isNull()) {
      p.drawImage(175, 10, hue_bar_image_);
      p.setPen(QPen(QColor(255, 255, 255, 64), 1.0));
      p.drawRect(QRectF(174.5, 9.5, 25.0, 181.0));
    }

    // SV indicator
    const double sx = 10.0 + saturation_ * 155.0;
    const double sy = 10.0 + (1.0 - value_) * 180.0;

    p.setPen(QPen(Qt::black, 2.0));
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(sx, sy), 4.5, 4.5);

    // Hue bar indicator
    const double hy = 10.0 + (hue_ / 360.0) * 180.0;
    p.setPen(QPen(QColor(25, 25, 38), 1.0));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(173.0, hy - 1.5, 28.0, 3.0));
  }
}

void StudioColorPicker::paint_preview_chip(QPainter &p, int width, int height) {
  const auto c = color();
  p.fillRect(0, 0, width, height, QColor(c.r, c.g, c.b));
  p.setPen(QPen(QColor(255, 255, 255, 64), 1.0));
  p.drawRect(QRectF(0.5, 0.5, width - 1.0, height - 1.0));
}

void StudioColorPicker::handle_mouse_press(double x, double y) {
  if (mode_ == ColorPickerMode::WHEEL) {
    const double d = std::hypot(x - 105.0, y - 100.0);
    if (d >= 70.0 && d <= 98.0) {
      drag_target_ = DragTarget::HUE_WHEEL;
    } else if (x >= 55.0 && x <= 155.0 && y >= 50.0 && y <= 150.0) {
      drag_target_ = DragTarget::SV_SQUARE;
    } else {
      drag_target_ = DragTarget::NONE;
    }
  } else {
    if (x >= 170.0 && x <= 205.0 && y >= 8.0 && y <= 192.0) {
      drag_target_ = DragTarget::HUE_BAR;
    } else if (x >= 8.0 && x <= 168.0 && y >= 8.0 && y <= 192.0) {
      drag_target_ = DragTarget::SV_BOX;
    } else {
      drag_target_ = DragTarget::NONE;
    }
  }

  update_from_mouse(x, y);
}

void StudioColorPicker::handle_mouse_move(double x, double y) {
  update_from_mouse(x, y);
}

void StudioColorPicker::handle_mouse_release() {
  if (drag_target_ != DragTarget::NONE) {
    drag_target_ = DragTarget::NONE;
    emit colorCommitted(color());
  }
}

void StudioColorPicker::update_from_mouse(double x, double y) {
  if (drag_target_ == DragTarget::NONE) {
    return;
  }

  if (drag_target_ == DragTarget::HUE_WHEEL) {
    double angle = std::atan2(y - 100.0, x - 105.0) * 180.0 / std::numbers::pi;
    if (angle < 0.0) angle += 360.0;
    hue_ = angle;
  } else if (drag_target_ == DragTarget::SV_SQUARE) {
    saturation_ = std::clamp((x - 57.0) / 96.0, 0.0, 1.0);
    value_ = std::clamp(1.0 - (y - 52.0) / 96.0, 0.0, 1.0);
  } else if (drag_target_ == DragTarget::HUE_BAR) {
    hue_ = std::clamp((y - 10.0) / 180.0, 0.0, 1.0) * 360.0;
    if (hue_ >= 360.0) hue_ = 0.0;
  } else if (drag_target_ == DragTarget::SV_BOX) {
    saturation_ = std::clamp((x - 10.0) / 155.0, 0.0, 1.0);
    value_ = std::clamp(1.0 - (y - 10.0) / 180.0, 0.0, 1.0);
  }

  update_surfaces();
  update_readouts();

  if (picker_area_) picker_area_->update();
  if (preview_chip_) preview_chip_->update();

  emit colorChanged(color());
}

} // namespace kreart::ui
