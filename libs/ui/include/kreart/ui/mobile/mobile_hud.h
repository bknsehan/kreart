#pragma once

#include <QTimer>
#include <QWidget>

namespace kreart::ui::mobile {

// Small translucent chip that shows one line of transient status text over the
// canvas ("Saved", "3 layers", errors). Never takes focus or pointer input.
class MobileHud : public QWidget {
  Q_OBJECT

public:
  explicit MobileHud(QWidget *host);

  void show_message(const QString &text, int ms = 2200);
  void hide_now();

  // Repositioned by the host on resize; anchor_y is the chip's bottom edge.
  void set_anchor(int center_x, int bottom_y);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QWidget *host_ = nullptr;
  QString text_;
  QTimer hide_timer_;
  int anchor_x_ = -1;
  int anchor_y_ = -1;
  qreal opacity_ = 1.0;
};

} // namespace kreart::ui::mobile
