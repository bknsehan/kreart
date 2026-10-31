#pragma once

#include <functional>

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QPropertyAnimation;
class QGraphicsOpacityEffect;

namespace kreart::ui::mobile {

// A bottom (portrait) or side (landscape) sheet that floats above the screen
// that owns it. The host must be the owning screen widget (MobileEditorView or
// MobileGallery), never the top-level window, so sheet geometry tracks the
// visible screen.
//
// Exactly-one-open is owned by the screen: each screen keeps its own
// `MobileSheet *active_sheet_` and closes the previous sheet before opening the
// next. This class holds no global registry.
class MobileSheet : public QWidget {
  Q_OBJECT

public:
  MobileSheet(QWidget *host, const QString &title, bool dismissible = true);
  ~MobileSheet() override;

  void set_body(QWidget *w);
  void open();
  void close();
  bool is_open() const { return is_open_; }

  void set_primary_action(const QString &label, std::function<void()> cb);
  void set_secondary_action(const QString &label, std::function<void()> cb);
  void clear_actions();

  // Layers sheet uses the full 86% height instead of the 560 px portrait cap.
  void set_full_height(bool full);
  // Invoked on every open(), after geometry is set. Lets a cached sheet rebuild
  // expensive content only while it is actually visible.
  void set_on_open(std::function<void()> cb) { on_open_ = std::move(cb); }
  // Recompute geometry (called by the host on resize while open).
  void relayout();

  QPushButton *primary_button() const { return primary_btn_; }
  QPushButton *secondary_button() const { return secondary_btn_; }
  QWidget *body() const { return body_; }
  QString title() const;

signals:
  void closed();

protected:
  void paintEvent(QPaintEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  QRect target_geometry() const;
  void animate_in();
  void finish_drag(int dy);

  QWidget *host_ = nullptr;
  QString title_;
  bool dismissible_ = true;
  bool is_open_ = false;
  bool full_height_ = false;

  // Child of the host, so it can be destroyed before this sheet.
  QPointer<QWidget> scrim_;
  QWidget *content_ = nullptr;
  QWidget *drag_handle_ = nullptr;
  QLabel *title_label_ = nullptr;
  QPushButton *close_btn_ = nullptr;
  QWidget *body_ = nullptr;
  QVBoxLayout *body_layout_ = nullptr;
  QWidget *footer_ = nullptr;
  QHBoxLayout *footer_layout_ = nullptr;
  QPushButton *secondary_btn_ = nullptr;
  QPushButton *primary_btn_ = nullptr;

  QGraphicsOpacityEffect *opacity_effect_ = nullptr;
  QPropertyAnimation *opacity_anim_ = nullptr;
  QPropertyAnimation *slide_anim_ = nullptr;

  int drag_start_y_ = 0;
  bool dragging_ = false;
  std::function<void()> on_open_;
};

} // namespace kreart::ui::mobile
