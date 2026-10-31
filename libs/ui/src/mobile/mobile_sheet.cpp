#include "kreart/ui/mobile/mobile_sheet.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_theme.h"

namespace kreart::ui::mobile {
namespace {

constexpr int kDragDismissPx = 96;
constexpr int kFooterButtonHeight = 48;

class Scrim : public QWidget {
public:
  explicit Scrim(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    setCursor(Qt::ArrowCursor);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
  }
};

class DragPill : public QWidget {
public:
  explicit DragPill(QWidget *parent) : QWidget(parent) {
    setFixedSize(44, 20);
    setCursor(Qt::SizeVerCursor);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 70));
    p.drawRoundedRect(QRect((width() - 36) / 2, (height() - 4) / 2, 36, 4), 2, 2);
  }
};

} // namespace

MobileSheet::MobileSheet(QWidget *host, const QString &title, bool dismissible)
    : QWidget(host), host_(host), title_(title), dismissible_(dismissible) {
  setObjectName("MobileSheet");
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::StrongFocus);
  hide();

  auto *scrim = new Scrim(host);
  scrim_ = scrim;
  scrim_->hide();
  scrim_->installEventFilter(this);

  content_ = new QWidget(this);
  content_->setObjectName("MobileSheet");
  content_->setAttribute(Qt::WA_StyledBackground, true);
  // Apply the sheet stylesheet to the sheet itself as well, so a sheet is
  // legible even when the application-level stylesheet is not installed.
  content_->setStyleSheet(sheet_qss());

  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(content_);

  auto *layout = new QVBoxLayout(content_);
  layout->setContentsMargins(20, 12, 20, 16);
  layout->setSpacing(10);

  drag_handle_ = new DragPill(content_);
  drag_handle_->installEventFilter(this);
  auto *handle_row = new QHBoxLayout();
  handle_row->setContentsMargins(0, 0, 0, 0);
  handle_row->addStretch(1);
  handle_row->addWidget(drag_handle_);
  handle_row->addStretch(1);
  layout->addLayout(handle_row);

  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, 0, 0);
  header->setSpacing(8);
  title_label_ = new QLabel(title_, content_);
  title_label_->setFont(font_section());
  header->addWidget(title_label_, 1);

  close_btn_ = new QPushButton(content_);
  close_btn_->setFixedSize(44, 44);
  close_btn_->setFlat(true);
  close_btn_->setCursor(Qt::PointingHandCursor);
  close_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/list-remove-symbolic.svg",
                                                tokens().text_muted, 20));
  close_btn_->setStyleSheet("background: transparent; border: none;");
  connect(close_btn_, &QPushButton::clicked, this, &MobileSheet::close);
  header->addWidget(close_btn_);
  layout->addLayout(header);

  body_ = new QWidget(content_);
  body_layout_ = new QVBoxLayout(body_);
  body_layout_->setContentsMargins(0, 0, 0, 0);
  body_layout_->setSpacing(10);
  layout->addWidget(body_, 1);

  footer_ = new QWidget(content_);
  footer_layout_ = new QHBoxLayout(footer_);
  footer_layout_->setContentsMargins(0, 0, 0, 0);
  footer_layout_->setSpacing(12);
  footer_layout_->addStretch(1);
  layout->addWidget(footer_);
  footer_->hide();

  opacity_effect_ = new QGraphicsOpacityEffect(content_);
  opacity_effect_->setOpacity(0.0);
  content_->setGraphicsEffect(opacity_effect_);
  opacity_anim_ = new QPropertyAnimation(opacity_effect_, "opacity", this);
  opacity_anim_->setDuration(120);
  opacity_anim_->setStartValue(0.0);
  opacity_anim_->setEndValue(1.0);

  slide_anim_ = new QPropertyAnimation(content_, "pos", this);
  slide_anim_->setDuration(180);
  slide_anim_->setEasingCurve(QEasingCurve::OutCubic);

  if (!dismissible_) {
    close_btn_->hide();
  }
}

MobileSheet::~MobileSheet() {
  // The scrim is a child of the host and may already be gone; QPointer makes
  // that safe. Hiding it here only avoids a stray overlay if this sheet dies
  // while still open.
  if (scrim_) scrim_->hide();
}

QString MobileSheet::title() const {
  return title_;
}

void MobileSheet::set_full_height(bool full) {
  full_height_ = full;
}

void MobileSheet::set_body(QWidget *w) {
  if (!w) return;
  body_layout_->addWidget(w);
}

void MobileSheet::set_primary_action(const QString &label, std::function<void()> cb) {
  if (!primary_btn_) {
    primary_btn_ = new QPushButton(footer_);
    primary_btn_->setProperty("accent", true);
    primary_btn_->setCursor(Qt::PointingHandCursor);
    // Explicit height in the style sheet: the sheet QSS sets min-height: 44px
    // on every button, which otherwise wins over setFixedHeight().
    primary_btn_->setStyleSheet(
        QString("QPushButton { min-height: %1px; max-height: %1px; border: none;"
                " border-radius: %2px; background: %3; color: %4; font-size: 15px;"
                " font-weight: 600; padding: 0 18px; }"
                "QPushButton:pressed { background: %5; }")
            .arg(kFooterButtonHeight)
            .arg(tokens().radius_control)
            .arg(css_color(tokens().accent))
            .arg(css_color(tokens().accent_ink))
            .arg(css_color(tokens().accent_pressed)));
    footer_layout_->addWidget(primary_btn_);
  }
  primary_btn_->setText(label);
  primary_btn_->disconnect();
  if (cb) {
    connect(primary_btn_, &QPushButton::clicked, this, [cb]() { cb(); });
  }
  footer_->show();
}

void MobileSheet::set_secondary_action(const QString &label, std::function<void()> cb) {
  if (!secondary_btn_) {
    secondary_btn_ = new QPushButton(footer_);
    secondary_btn_->setProperty("ghost", true);
    secondary_btn_->setCursor(Qt::PointingHandCursor);
    secondary_btn_->setStyleSheet(
        QString("QPushButton { min-height: %1px; max-height: %1px; border: none;"
                " border-radius: %2px; background: transparent; color: %3;"
                " font-size: 14px; padding: 0 18px; }"
                "QPushButton:pressed { background: #2A2E36; }")
            .arg(kFooterButtonHeight)
            .arg(tokens().radius_control)
            .arg(css_color(tokens().text_muted)));
    footer_layout_->insertWidget(0, secondary_btn_);
  }
  secondary_btn_->setText(label);
  secondary_btn_->disconnect();
  if (cb) {
    connect(secondary_btn_, &QPushButton::clicked, this, [cb]() { cb(); });
  }
  footer_->show();
}

void MobileSheet::clear_actions() {
  if (primary_btn_) {
    primary_btn_->disconnect();
    primary_btn_->deleteLater();
    primary_btn_ = nullptr;
  }
  if (secondary_btn_) {
    secondary_btn_->disconnect();
    secondary_btn_->deleteLater();
    secondary_btn_ = nullptr;
  }
  footer_->hide();
}

QRect MobileSheet::target_geometry() const {
  const int host_w = host_ ? host_->width() : 0;
  const int host_h = host_ ? host_->height() : 0;
  if (host_w <= 0 || host_h <= 0) return {};

  const bool landscape = host_w > host_h;
  if (landscape) {
    const int w = std::min(static_cast<int>(host_w * 0.46), 420);
    return QRect(host_w - w, 0, w, host_h);
  }
  int h = static_cast<int>(host_h * 0.86);
  if (!full_height_) {
    h = std::min(h, 560);
  }
  return QRect(0, host_h - h, host_w, h);
}

void MobileSheet::relayout() {
  if (!is_open_) return;
  const QRect target = target_geometry();
  if (target.isEmpty()) return;
  setGeometry(target);
  content_->setGeometry(0, 0, target.width(), target.height());
  if (scrim_) scrim_->setGeometry(host_->rect());
}

void MobileSheet::open() {
  if (!host_) return;
  if (host_->width() <= 0 || host_->height() <= 0) return;
  const QRect target = target_geometry();
  if (target.isEmpty()) return;

  const bool was_open = is_open_;
  is_open_ = true;
  setGeometry(target);
  content_->setGeometry(0, 0, target.width(), target.height());

  if (scrim_) {
    scrim_->setGeometry(host_->rect());
    scrim_->show();
    scrim_->raise();
  }
  show();
  raise();

  if (on_open_) on_open_();

  if (was_open) {
    // Already open (retargeted): no re-entry animation.
    opacity_effect_->setOpacity(1.0);
    return;
  }

  const bool landscape = host_->width() > host_->height();
  const QPoint start = landscape ? QPoint(target.width(), 0)
                                 : QPoint(0, target.height());
  slide_anim_->stop();
  slide_anim_->setStartValue(start);
  slide_anim_->setEndValue(QPoint(0, 0));
  slide_anim_->start();
  opacity_anim_->stop();
  opacity_anim_->start();
  setFocus(Qt::OtherFocusReason);
}

void MobileSheet::close() {
  if (!is_open_) return;
  is_open_ = false;
  slide_anim_->stop();
  opacity_anim_->stop();
  opacity_effect_->setOpacity(0.0);
  if (scrim_) scrim_->hide();
  hide();
  emit closed();
}

void MobileSheet::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  const QRect target = target_geometry();
  if (!target.isEmpty() && geometry() != target) {
    setGeometry(target);
    content_->setGeometry(0, 0, target.width(), target.height());
  }
}

void MobileSheet::paintEvent(QPaintEvent *) {
  // The visible surface is content_ (styled via QSS); this widget only hosts it
  // so the slide-in animation can translate the surface inside a stable rect.
}

void MobileSheet::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
    if (dismissible_) {
      close();
    }
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

bool MobileSheet::eventFilter(QObject *obj, QEvent *event) {
  if (obj == scrim_) {
    if (event->type() == QEvent::MouseButtonRelease) {
      auto *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::LeftButton && dismissible_) {
        close();
        return true;
      }
    }
    return false;
  }

  if (obj == drag_handle_) {
    if (event->type() == QEvent::MouseButtonPress) {
      auto *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::LeftButton) {
        dragging_ = true;
        drag_start_y_ = static_cast<int>(me->globalPosition().y());
        return true;
      }
    } else if (event->type() == QEvent::MouseMove && dragging_) {
      auto *me = static_cast<QMouseEvent *>(event);
      const int dy = static_cast<int>(me->globalPosition().y()) - drag_start_y_;
      const QRect target = target_geometry();
      if (!target.isEmpty()) {
        const int shift = std::clamp(dy, 0, target.height());
        setGeometry(target.translated(0, shift));
        content_->move(0, 0);
      }
      return true;
    } else if (event->type() == QEvent::MouseButtonRelease && dragging_) {
      auto *me = static_cast<QMouseEvent *>(event);
      dragging_ = false;
      finish_drag(static_cast<int>(me->globalPosition().y()) - drag_start_y_);
      return true;
    }
  }
  return QWidget::eventFilter(obj, event);
}

void MobileSheet::finish_drag(int dy) {
  if (dismissible_ && dy > kDragDismissPx) {
    close();
    return;
  }
  relayout();
}

} // namespace kreart::ui::mobile
