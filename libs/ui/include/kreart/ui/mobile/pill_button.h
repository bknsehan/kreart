#pragma once

#include <QPushButton>

namespace kreart::ui::mobile {

// Circular or rounded touch button used across the mobile chrome. Variants map
// to the Ink & Amber palette; the QSS for sheets lives in mobile_theme.h, while
// these styles are applied inline so they also work outside sheets.
class PillButton : public QPushButton {
  Q_OBJECT

public:
  enum class Variant {
    Ghost,
    Surface,
    Accent,
    Danger,
  };

  explicit PillButton(QWidget *parent = nullptr);
  PillButton(const QString &text, QWidget *parent = nullptr);

  void set_variant(Variant variant);
  Variant variant() const { return variant_; }

  void set_icon_name(const QString &resource_path);
  void set_badge(int count);
  int badge() const { return badge_; }

  void set_circular(bool circular);
  bool is_circular() const { return circular_; }

  void set_touch_size(int size);

protected:
  void paintEvent(QPaintEvent *event) override;
  QSize sizeHint() const override;

private:
  void apply_style();

  Variant variant_ = Variant::Surface;
  QString icon_path_;
  int badge_ = 0;
  bool circular_ = false;
  int touch_size_ = 48;
};

} // namespace kreart::ui::mobile
