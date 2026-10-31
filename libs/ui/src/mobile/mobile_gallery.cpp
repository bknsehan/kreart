#include "kreart/ui/mobile/mobile_gallery.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScroller>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include "kreart/core/document.h"
#include "kreart/io/kra.h"
#include "kreart/io/png.h"
#include "kreart/mobile/android_utils.h"
#include "kreart/ui/app_settings.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_hud.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/pill_button.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

constexpr int kCardGap = 12;
constexpr int kGridMargin = 12;
constexpr int kThumbAspectH = 10;
constexpr int kThumbAspectW = 16;
// Above these limits the composite fallback is skipped: reading and
// compositing a large multi-layer document stalls a phone for seconds.
constexpr std::size_t kMaxCompositeLayers = 6;
constexpr int kMaxCompositeDimension = 4096;

bool is_image_all_transparent(const QImage &img) {
  if (img.isNull()) return true;
  const QImage argb = (img.format() == QImage::Format_ARGB32_Premultiplied ||
                       img.format() == QImage::Format_ARGB32)
                          ? img
                          : img.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < argb.height(); ++y) {
    const QRgb *scan = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
    for (int x = 0; x < argb.width(); ++x) {
      if (qAlpha(scan[x]) > 0) return false;
    }
  }
  return true;
}

// Rounded 16:10 thumbnail with a checkerboard behind transparent art.
QPixmap make_card_thumbnail(const QPixmap &source, int target_w, int target_h) {
  QPixmap result(target_w, target_h);
  result.fill(Qt::transparent);

  QPainter p(&result);
  p.setRenderHint(QPainter::Antialiasing, true);

  QPainterPath clip;
  clip.addRoundedRect(QRectF(0, 0, target_w, target_h), 16, 16);
  p.setClipPath(clip);

  // Checkerboard so transparent documents read as transparent.
  const int check = 10;
  for (int y = 0; y < target_h; y += check) {
    for (int x = 0; x < target_w; x += check) {
      const bool alt = ((x / check) + (y / check)) % 2 == 0;
      p.fillRect(QRect(x, y, check, check), alt ? QColor(46, 49, 56) : QColor(36, 38, 44));
    }
  }

  if (!source.isNull()) {
    const QPixmap scaled = source.scaled(target_w, target_h, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    const int ox = (target_w - scaled.width()) / 2;
    const int oy = (target_h - scaled.height()) / 2;
    p.drawPixmap(ox, oy, scaled);
  } else {
    p.setPen(tokens().text_muted);
    p.setFont(font_caption());
    p.drawText(QRect(0, 0, target_w, target_h), Qt::AlignCenter, QObject::tr("No preview"));
  }

  p.setClipping(false);
  p.setPen(QPen(QColor(255, 255, 255, 22), 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(QRectF(0.5, 0.5, target_w - 1.0, target_h - 1.0), 16, 16);
  return result;
}

// Live aspect-ratio preview for the new-canvas sheet.
class RatioPreview : public QWidget {
public:
  explicit RatioPreview(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void set_dimensions(int w, int h) {
    width_ = std::max(1, w);
    height_ = std::max(1, h);
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double aspect = static_cast<double>(width_) / std::max(1, height_);
    const double avail_w = width() - 32.0;
    const double avail_h = height() - 24.0;
    double box_w = avail_w;
    double box_h = box_w / aspect;
    if (box_h > avail_h) {
      box_h = avail_h;
      box_w = box_h * aspect;
    }
    const QRectF box((width() - box_w) * 0.5, (height() - box_h) * 0.5, box_w, box_h);
    p.setPen(QPen(tokens().accent, 2.0));
    p.setBrush(QColor(30, 33, 39, 220));
    p.drawRoundedRect(box, 8, 8);
    p.setPen(tokens().text_muted);
    p.setFont(font_caption());
    p.drawText(box.adjusted(6, 6, -6, -6), Qt::AlignCenter,
               QString("%1 \u00d7 %2").arg(width_).arg(height_));
  }

private:
  int width_ = 1920;
  int height_ = 1080;
};

} // namespace

// ---------------------------------------------------------------------------
// Card
// ---------------------------------------------------------------------------

class MobileGalleryCard : public QWidget {
public:
  MobileGalleryCard(const MobileProjectItem &item, int card_width,
                    std::function<void()> on_open, std::function<void()> on_actions,
                    QWidget *parent)
      : QWidget(parent), item_(item), on_open_(std::move(on_open)),
        on_actions_(std::move(on_actions)) {
    setCursor(Qt::PointingHandCursor);
    set_card_width(card_width);

    more_btn_ = new QPushButton(QString::fromUtf8("\u22ef"), this);
    more_btn_->setFixedSize(44, 44);
    more_btn_->setCursor(Qt::PointingHandCursor);
    more_btn_->setStyleSheet(
        QString("QPushButton { background: transparent; border: none; color: %1;"
                " font-size: 20px; }")
            .arg(css_color(tokens().text_muted)));
    connect(more_btn_, &QPushButton::clicked, this, [this]() {
      if (on_actions_) on_actions_();
    });
    position_more_button();
  }

  void set_card_width(int card_width) {
    card_width_ = std::max(120, card_width);
    thumb_h_ = std::max(60, card_width_ * kThumbAspectH / kThumbAspectW);
    // Thumbnail + 6 px gap + two text lines.
    const QFontMetrics body_metrics(font_body());
    const QFontMetrics caption_metrics(font_caption());
    text_h_ = body_metrics.height() + caption_metrics.height();
    setFixedSize(card_width_, thumb_h_ + 6 + std::max(44, text_h_));
    thumb_cache_ = make_card_thumbnail(item_.thumbnail_pixmap, card_width_, thumb_h_);
    position_more_button();
    update();
  }

  const MobileProjectItem &item() const { return item_; }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawPixmap(0, 0, thumb_cache_);

    const int text_y = thumb_h_ + 6;
    const int text_w = std::max(40, card_width_ - 52);

    p.setFont(font_body());
    p.setPen(tokens().text);
    const QFontMetrics body_metrics(font_body());
    p.drawText(QRect(2, text_y, text_w, body_metrics.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               body_metrics.elidedText(item_.title, Qt::ElideRight, text_w));

    p.setFont(font_caption());
    p.setPen(tokens().text_muted);
    const QFontMetrics caption_metrics(font_caption());
    p.drawText(QRect(2, text_y + body_metrics.height(), text_w, caption_metrics.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString("%1\u00d7%2 \u2022 %3")
                   .arg(item_.width)
                   .arg(item_.height)
                   .arg(item_.last_modified.toString("d MMM")));
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      press_pos_ = event->position().toPoint();
      pressed_ = true;
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && pressed_) {
      pressed_ = false;
      const QPoint delta = event->position().toPoint() - press_pos_;
      // Activation happens on release, never on press, and never after a drag.
      if (delta.manhattanLength() < 10) {
        bool scrolling = false;
        for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
          if (auto *area = qobject_cast<QScrollArea *>(w)) {
            if (QScroller::scroller(area->viewport())->state() != QScroller::Inactive) {
              scrolling = true;
            }
            break;
          }
        }
        if (!scrolling && on_open_) on_open_();
      }
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

private:
  void position_more_button() {
    if (!more_btn_) return;
    more_btn_->move(card_width_ - 46, thumb_h_ + 4);
  }

  MobileProjectItem item_;
  std::function<void()> on_open_;
  std::function<void()> on_actions_;
  QPushButton *more_btn_ = nullptr;
  QPixmap thumb_cache_;
  QPoint press_pos_;
  bool pressed_ = false;
  int card_width_ = 240;
  int thumb_h_ = 150;
  int text_h_ = 40;
};

// ---------------------------------------------------------------------------
// Gallery
// ---------------------------------------------------------------------------

MobileGallery::MobileGallery(QWidget *parent) : QWidget(parent) {
  setObjectName("MobileGallery");
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet(QString("QWidget#MobileGallery { background: %1; }")
                    .arg(css_color(tokens().workspace_bg)));
  build_ui();
  refresh_projects();
}

MobileGallery::~MobileGallery() = default;

int MobileGallery::card_width() const {
  if (columns_ <= 0) return 0;
  const int usable = width() - 2 * kGridMargin - (columns_ - 1) * kCardGap;
  return std::max(1, usable / columns_);
}

void MobileGallery::build_ui() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // Header
  auto *header = new QWidget(this);
  header->setFixedHeight(64);
  auto *header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(16, 8, 8, 8);
  header_layout->setSpacing(8);

  auto *wordmark = new QLabel(header);
  wordmark->setText(QStringLiteral("Kreart"));
  wordmark->setFont(font_title());
  wordmark->setStyleSheet(QString("color: %1;").arg(css_color(tokens().accent)));
  header_layout->addWidget(wordmark);
  header_layout->addStretch(1);

  overflow_btn_ = new QPushButton(QString::fromUtf8("\u22ef"), header);
  overflow_btn_->setFixedSize(48, 48);
  overflow_btn_->setCursor(Qt::PointingHandCursor);
  overflow_btn_->setStyleSheet(
      QString("QPushButton { background: #1E2127; border: none; border-radius: 24px;"
              " color: %1; font-size: 20px; }"
              "QPushButton:pressed { background: #2A2E36; }")
          .arg(css_color(tokens().text)));
  connect(overflow_btn_, &QPushButton::clicked, this, [this]() { show_overview_sheet(); });
  header_layout->addWidget(overflow_btn_);
  root->addWidget(header);

  // Content
  scroll_area_ = new QScrollArea(this);
  scroll_area_->setWidgetResizable(true);
  scroll_area_->setFrameShape(QFrame::NoFrame);
  scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  QScroller::grabGesture(scroll_area_->viewport(), QScroller::TouchGesture);

  grid_container_ = new QWidget(scroll_area_);
  grid_layout_ = new QGridLayout(grid_container_);
  grid_layout_->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
  grid_layout_->setHorizontalSpacing(kCardGap);
  grid_layout_->setVerticalSpacing(kCardGap);
  scroll_area_->setWidget(grid_container_);
  // The viewport and grid must match the workspace background, otherwise the
  // area below the last row falls back to the light default palette.
  scroll_area_->setStyleSheet(
      QString("QScrollArea { background: %1; border: none; }"
              "QScrollArea > QWidget > QWidget { background: %1; }")
          .arg(css_color(tokens().workspace_bg)));
  scroll_area_->viewport()->setAutoFillBackground(false);
  grid_container_->setAttribute(Qt::WA_StyledBackground, true);
  grid_container_->setStyleSheet(
      QString("background: %1;").arg(css_color(tokens().workspace_bg)));
  root->addWidget(scroll_area_, 1);

  // Empty state
  empty_container_ = new QWidget(this);
  auto *empty_layout = new QVBoxLayout(empty_container_);
  empty_layout->setContentsMargins(24, 24, 24, 24);
  empty_layout->setSpacing(16);
  empty_layout->addStretch(1);

  auto *glyph = new QLabel(empty_container_);
  glyph->setAlignment(Qt::AlignCenter);
  glyph->setPixmap(IconHelper::render_symbolic(":/icons/icons/tool-brush-symbolic.svg",
                                               tokens().accent, 64));
  empty_layout->addWidget(glyph);

  empty_label_ = new QLabel(tr("Start something"), empty_container_);
  empty_label_->setAlignment(Qt::AlignCenter);
  empty_label_->setFont(font_title());
  empty_label_->setStyleSheet(QString("color: %1;").arg(css_color(tokens().text)));
  empty_layout->addWidget(empty_label_);

  auto *empty_buttons = new QHBoxLayout();
  empty_buttons->setSpacing(12);
  empty_buttons->addStretch(1);
  auto *new_btn = new QPushButton(tr("New Canvas"), empty_container_);
  new_btn->setMinimumHeight(52);
  new_btn->setMinimumWidth(150);
  new_btn->setCursor(Qt::PointingHandCursor);
  new_btn->setProperty("accent", true);
  new_btn->setStyleSheet(
      QString("QPushButton { background: %1; color: %2; border: none; border-radius: 14px;"
              " font-size: 15px; font-weight: 600; padding: 0 20px; }")
          .arg(css_color(tokens().accent))
          .arg(css_color(tokens().accent_ink)));
  connect(new_btn, &QPushButton::clicked, this, [this]() { show_new_canvas_sheet(); });
  empty_buttons->addWidget(new_btn);

  auto *import_btn = new QPushButton(tr("Import"), empty_container_);
  import_btn->setMinimumHeight(52);
  import_btn->setMinimumWidth(120);
  import_btn->setCursor(Qt::PointingHandCursor);
  import_btn->setStyleSheet(
      QString("QPushButton { background: #1E2127; color: %1; border: none; border-radius: 14px;"
              " font-size: 15px; padding: 0 20px; }")
          .arg(css_color(tokens().text)));
  connect(import_btn, &QPushButton::clicked, this, [this]() {
    emit importProjectRequested();
  });
  empty_buttons->addWidget(import_btn);
  empty_buttons->addStretch(1);
  empty_layout->addLayout(empty_buttons);
  empty_layout->addStretch(1);
  root->addWidget(empty_container_, 1);
  empty_container_->hide();

  hud_ = new MobileHud(this);
}

QWidget *MobileGallery::card_at(int index) const {
  if (index < 0 || index >= static_cast<int>(cards_.size())) return nullptr;
  return cards_[static_cast<std::size_t>(index)];
}

void MobileGallery::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  const bool landscape = width() > height();
  const int desired = landscape ? 3 : 2;
  if (desired != columns_) {
    columns_ = desired;
    rebuild_grid();
  } else {
    for (auto *card : cards_) {
      card->set_card_width(card_width());
    }
  }
  if (hud_) hud_->set_anchor(width() / 2, height() - 24);
}

void MobileGallery::hud_message(const QString &text) {
  if (hud_) hud_->show_message(text);
}

void MobileGallery::refresh_projects() {
  projects_.clear();
  const std::filesystem::path dir = kreart::mobile::mobile_projects_dir();
  std::error_code ec;

  if (std::filesystem::is_directory(dir, ec)) {
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      if (entry.path().extension() != ".kra") continue;

      MobileProjectItem item;
      item.file_path = entry.path();
      item.title = QString::fromStdString(entry.path().stem().string());

      const std::filesystem::path thumb_path =
          entry.path().parent_path() / (entry.path().stem().string() + ".thumb.png");

      auto mtime = std::filesystem::last_write_time(entry.path(), ec);
      if (!ec) {
        const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            mtime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        item.last_modified = QDateTime::fromSecsSinceEpoch(std::chrono::system_clock::to_time_t(sctp));
      }

      const QString cache_key =
          QString::fromStdString(item.file_path.string()) + "|" +
          QString::number(item.last_modified.toSecsSinceEpoch());

      if (thumbnail_cache_.contains(cache_key)) {
        item.thumbnail_pixmap = thumbnail_cache_.value(cache_key);
        if (!item.thumbnail_pixmap.isNull()) item.thumbnail_path = thumb_path;
      }

      bool thumb_valid = !item.thumbnail_pixmap.isNull();
      if (!thumb_valid && std::filesystem::exists(thumb_path, ec)) {
        QImage thumb_img(QString::fromStdString(thumb_path.string()));
        if (!thumb_img.isNull() && !is_image_all_transparent(thumb_img)) {
          item.thumbnail_path = thumb_path;
          item.thumbnail_pixmap = QPixmap::fromImage(thumb_img);
          thumb_valid = true;
        }
      }

      std::vector<std::uint8_t> png_bytes;
      std::string err;
      int kra_w = 0;
      int kra_h = 0;
      if (io::read_kra_thumbnail(entry.path(), png_bytes, kra_w, kra_h, err)) {
        if (kra_w > 0 && kra_h > 0) {
          item.width = kra_w;
          item.height = kra_h;
        }
        if (!thumb_valid && !png_bytes.empty()) {
          QImage kra_thumb;
          kra_thumb.loadFromData(png_bytes.data(), static_cast<uint>(png_bytes.size()));
          if (!kra_thumb.isNull() && !is_image_all_transparent(kra_thumb)) {
            QFile out(QString::fromStdString(thumb_path.string()));
            if (out.open(QIODevice::WriteOnly)) {
              out.write(reinterpret_cast<const char *>(png_bytes.data()),
                        static_cast<qint64>(png_bytes.size()));
              out.close();
              item.thumbnail_path = thumb_path;
            }
            item.thumbnail_pixmap = QPixmap::fromImage(kra_thumb);
            thumb_valid = true;
          }
        }
      }

      // Full-document composite fallback is deliberately skipped for large or
      // deeply layered documents: it stalls a phone for seconds.
      if (!thumb_valid) {
        core::Document probe(1, 1);
        std::string read_err;
        if (io::read_kra(entry.path(), probe, read_err)) {
          item.width = probe.width();
          item.height = probe.height();
          const bool too_big = probe.layer_count() > kMaxCompositeLayers ||
                               probe.width() > kMaxCompositeDimension ||
                               probe.height() > kMaxCompositeDimension;
          if (!too_big) {
            probe.mark_composite_dirty();
            const auto composite = probe.composite_with_floating();
            if (composite.content_bounds().has_value()) {
              std::string png_err;
              if (io::write_png(thumb_path, composite, png_err)) {
                item.thumbnail_path = thumb_path;
                item.thumbnail_pixmap.load(QString::fromStdString(thumb_path.string()));
                thumb_valid = true;
              }
            }
          }
        }
      }

      if (!item.thumbnail_pixmap.isNull()) {
        thumbnail_cache_.insert(cache_key, item.thumbnail_pixmap);
      }

      projects_.push_back(std::move(item));
    }
  }

  std::sort(projects_.begin(), projects_.end(),
            [](const MobileProjectItem &a, const MobileProjectItem &b) {
              return a.last_modified > b.last_modified;
            });

  rebuild_grid();
}

void MobileGallery::rebuild_grid() {
  cards_.clear();
  QLayoutItem *child = nullptr;
  while ((child = grid_layout_->takeAt(0)) != nullptr) {
    delete child->widget();
    delete child;
  }

  const bool empty = projects_.empty();
  empty_container_->setVisible(empty);
  scroll_area_->setVisible(!empty);
  if (empty) return;

  const int card_w = card_width();
  for (std::size_t i = 0; i < projects_.size(); ++i) {
    const auto &item = projects_[i];
    auto *card = new MobileGalleryCard(
        item, card_w,
        [this, path = item.file_path]() { emit openProject(path); },
        [this, item]() { show_card_actions(item); },
        grid_container_);
    const int row = static_cast<int>(i) / columns_;
    const int col = static_cast<int>(i) % columns_;
    grid_layout_->addWidget(card, row, col);
    cards_.push_back(card);
  }
  grid_layout_->setRowStretch(static_cast<int>(projects_.size()) / columns_ + 1, 1);
}

// ---------------------------------------------------------------------------
// Sheets
// ---------------------------------------------------------------------------

void MobileGallery::show_card_actions(const MobileProjectItem &item) {
  if (active_sheet_) active_sheet_->close();

  auto *sheet = new MobileSheet(this, item.title, true);
  active_sheet_ = sheet;

  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto add_row = [&](const QString &label, std::function<void()> fn, bool danger = false) {
    auto *btn = new QPushButton(label, body);
    btn->setMinimumHeight(52);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        QString("QPushButton { background: #1E2127; color: %1; border: none;"
                " border-radius: 12px; padding: 0 16px; font-size: 14px; text-align: left; }"
                "QPushButton:pressed { background: #2A2E36; }")
            .arg(danger ? css_color(tokens().danger) : css_color(tokens().text)));
    connect(btn, &QPushButton::clicked, sheet, [sheet, fn]() {
      sheet->close();
      if (fn) fn();
    });
    layout->addWidget(btn);
  };

  add_row(tr("Rename"), [this, item]() { show_rename_sheet(item); });
  add_row(tr("Duplicate"), [this, item]() { duplicate_project(item); });
  add_row(tr("Share / Export PNG"), [this, item]() { export_project_png(item); });
  add_row(tr("Delete"), [this, item]() { show_delete_confirm(item); }, true);

  sheet->set_body(body);
  connect(sheet, &MobileSheet::closed, this, [this, sheet]() {
    if (active_sheet_ == sheet) active_sheet_ = nullptr;
    sheet->deleteLater();
  });
  sheet->open();
}

void MobileGallery::show_rename_sheet(const MobileProjectItem &item) {
  if (active_sheet_) active_sheet_->close();

  auto *sheet = new MobileSheet(this, tr("Rename"), true);
  active_sheet_ = sheet;

  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto *edit = new QLineEdit(body);
  edit->setMinimumHeight(44);
  edit->setText(item.title);
  edit->selectAll();
  auto *error = new QLabel(body);
  error->setStyleSheet("color: #F45C5C; font-size: 12px;");
  error->hide();
  layout->addWidget(edit);
  layout->addWidget(error);
  sheet->set_body(body);

  auto submit = [this, item, sheet, edit, error]() {
    const QString name = edit->text().trimmed();
    QString message;
    if (!kreart::mobile::is_valid_file_stem(name, message)) {
      error->setText(message);
      error->show();
      return;
    }
    const std::filesystem::path target =
        item.file_path.parent_path() / (name.toStdString() + ".kra");
    std::error_code ec;
    if (std::filesystem::exists(target, ec) &&
        !std::filesystem::equivalent(target, item.file_path, ec)) {
      error->setText(tr("A project with that name already exists"));
      error->show();
      return;
    }
    std::filesystem::rename(item.file_path, target, ec);
    if (ec) {
      error->setText(tr("Rename failed"));
      error->show();
      return;
    }
    if (!item.thumbnail_path.empty() && std::filesystem::exists(item.thumbnail_path, ec)) {
      const std::filesystem::path new_thumb =
          item.file_path.parent_path() / (name.toStdString() + ".thumb.png");
      std::filesystem::rename(item.thumbnail_path, new_thumb, ec);
    }
    sheet->close();
    refresh_projects();
    hud_message(tr("Renamed"));
  };
  connect(edit, &QLineEdit::returnPressed, body, submit);
  sheet->set_secondary_action(tr("Cancel"), [sheet]() { sheet->close(); });
  sheet->set_primary_action(tr("Save"), submit);

  connect(sheet, &MobileSheet::closed, this, [this, sheet]() {
    if (active_sheet_ == sheet) active_sheet_ = nullptr;
    sheet->deleteLater();
  });
  sheet->open();
}

void MobileGallery::show_delete_confirm(const MobileProjectItem &item) {
  if (active_sheet_) active_sheet_->close();

  auto *sheet = new MobileSheet(this, tr("Delete"), true);
  active_sheet_ = sheet;

  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *label = new QLabel(tr("Delete '%1'? This cannot be undone.").arg(item.title), body);
  label->setWordWrap(true);
  layout->addWidget(label);
  sheet->set_body(body);

  sheet->set_secondary_action(tr("Cancel"), [sheet]() { sheet->close(); });
  sheet->set_primary_action(tr("Delete"), [this, item, sheet]() {
    std::error_code ec;
    std::filesystem::remove(item.file_path, ec);
    if (!item.thumbnail_path.empty()) std::filesystem::remove(item.thumbnail_path, ec);
    sheet->close();
    refresh_projects();
    hud_message(tr("Deleted"));
  });
  if (sheet->primary_button()) {
    sheet->primary_button()->setProperty("accent", false);
    sheet->primary_button()->setProperty("danger", true);
    sheet->primary_button()->style()->unpolish(sheet->primary_button());
    sheet->primary_button()->style()->polish(sheet->primary_button());
  }

  connect(sheet, &MobileSheet::closed, this, [this, sheet]() {
    if (active_sheet_ == sheet) active_sheet_ = nullptr;
    sheet->deleteLater();
  });
  sheet->open();
}

void MobileGallery::duplicate_project(const MobileProjectItem &item) {
  std::error_code ec;
  const std::filesystem::path dir = item.file_path.parent_path();
  const std::string stem = item.file_path.stem().string() + " Copy";
  const std::filesystem::path target = kreart::mobile::unique_path_in(dir, stem, ".kra");
  std::filesystem::copy_file(item.file_path, target, ec);
  if (ec) {
    hud_message(tr("Duplicate failed"));
    return;
  }
  if (!item.thumbnail_path.empty() && std::filesystem::exists(item.thumbnail_path, ec)) {
    const std::filesystem::path new_thumb =
        dir / (target.stem().string() + ".thumb.png");
    std::filesystem::copy_file(item.thumbnail_path, new_thumb,
                               std::filesystem::copy_options::overwrite_existing, ec);
  }
  refresh_projects();
  hud_message(tr("Duplicated"));
}

void MobileGallery::export_project_png(const MobileProjectItem &item) {
  core::Document doc(1, 1);
  std::string err;
  if (!io::read_kra(item.file_path, doc, err)) {
    hud_message(tr("Could not read the project"));
    return;
  }
  doc.mark_composite_dirty();
  const auto composite = doc.composite_with_floating();
  const std::filesystem::path target =
      kreart::mobile::unique_path_in(item.file_path.parent_path(), item.file_path.stem().string(), ".png");
  std::string png_err;
  if (!io::write_png(target, composite, png_err)) {
    hud_message(tr("Export failed"));
    return;
  }
  hud_message(tr("Exported %1").arg(QString::fromStdString(target.filename().string())));
}

void MobileGallery::show_overview_sheet() {
  if (active_sheet_) active_sheet_->close();

  auto *sheet = new MobileSheet(this, tr("Gallery"), true);
  active_sheet_ = sheet;

  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto add_row = [&](const QString &label, std::function<void()> fn) {
    auto *btn = new QPushButton(label, body);
    btn->setMinimumHeight(52);
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QPushButton::clicked, sheet, [sheet, fn]() {
      sheet->close();
      if (fn) fn();
    });
    layout->addWidget(btn);
  };

  add_row(tr("New Canvas"), [this]() { show_new_canvas_sheet(); });
  add_row(tr("Import project or image"), [this]() { emit importProjectRequested(); });
  add_row(tr("Blank canvas"), [this]() { emit openBlankCanvasRequested(); });

  auto *folder = new QLabel(
      tr("Projects folder\n%1").arg(QString::fromStdString(kreart::mobile::mobile_projects_dir().string())),
      body);
  folder->setProperty("muted", true);
  folder->setWordWrap(true);
  layout->addWidget(folder);

  auto *startup_label = new QLabel(tr("Startup: %1")
                                       .arg(AppSettings::instance().startup_mode() ==
                                                    StartupMode::BLANK_CANVAS
                                                ? tr("Blank canvas")
                                                : tr("Gallery")),
                                   body);
  startup_label->setProperty("muted", true);
  layout->addWidget(startup_label);

  sheet->set_body(body);
  connect(sheet, &MobileSheet::closed, this, [this, sheet]() {
    if (active_sheet_ == sheet) active_sheet_ = nullptr;
    sheet->deleteLater();
  });
  sheet->open();
}

void MobileGallery::show_new_canvas_sheet() {
  if (active_sheet_) active_sheet_->close();

  auto *sheet = new MobileSheet(this, tr("New Canvas"), true);
  active_sheet_ = sheet;

  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto *preview = new RatioPreview(body);
  preview->set_dimensions(1920, 1080);
  layout->addWidget(preview);

  auto *width_spin = new QSpinBox(body);
  width_spin->setRange(64, 8192);
  width_spin->setValue(1920);
  width_spin->setMinimumHeight(44);
  auto *height_spin = new QSpinBox(body);
  height_spin->setRange(64, 8192);
  height_spin->setValue(1080);
  height_spin->setMinimumHeight(44);

  auto *dim_row = new QHBoxLayout();
  dim_row->setSpacing(8);
  dim_row->addWidget(new QLabel(tr("W"), body));
  dim_row->addWidget(width_spin, 1);
  auto *swap_btn = new QPushButton(body);
  swap_btn->setFixedSize(44, 44);
  swap_btn->setCursor(Qt::PointingHandCursor);
  swap_btn->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-swap-symbolic.svg",
                                              tokens().text, 20));
  swap_btn->setStyleSheet("background: #1E2127; border: none; border-radius: 12px;");
  dim_row->addWidget(swap_btn);
  dim_row->addWidget(new QLabel(tr("H"), body));
  dim_row->addWidget(height_spin, 1);
  layout->addLayout(dim_row);

  auto sync_preview = [preview, width_spin, height_spin]() {
    preview->set_dimensions(width_spin->value(), height_spin->value());
  };
  connect(width_spin, &QSpinBox::valueChanged, body, [sync_preview](int) { sync_preview(); });
  connect(height_spin, &QSpinBox::valueChanged, body, [sync_preview](int) { sync_preview(); });
  connect(swap_btn, &QPushButton::clicked, body, [width_spin, height_spin, sync_preview]() {
    const int w = width_spin->value();
    width_spin->setValue(height_spin->value());
    height_spin->setValue(w);
    sync_preview();
  });

  // Preset chips
  struct Preset {
    const char *label;
    int w;
    int h;
  };
  const Preset presets[] = {{QT_TR_NOOP("Full HD Landscape"), 1920, 1080},
                            {QT_TR_NOOP("Full HD Portrait"), 1080, 1920},
                            {QT_TR_NOOP("Square"), 2048, 2048},
                            {QT_TR_NOOP("4K"), 3840, 2160},
                            {QT_TR_NOOP("A4"), 2480, 3508}};
  auto *chip_host = new QWidget(body);
  auto *chip_layout = new QGridLayout(chip_host);
  chip_layout->setContentsMargins(0, 0, 0, 0);
  chip_layout->setSpacing(6);
  int chip_index = 0;
  for (const auto &preset : presets) {
    auto *chip = new QPushButton(tr(preset.label), chip_host);
    chip->setMinimumHeight(44);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setStyleSheet(
        "QPushButton { background: #1E2127; color: #ECEEF3; border: none; border-radius: 12px;"
        " padding: 0 10px; font-size: 12px; }"
        "QPushButton:pressed { background: #2A2E36; }");
    connect(chip, &QPushButton::clicked, chip_host, [width_spin, height_spin, preset, sync_preview]() {
      width_spin->setValue(preset.w);
      height_spin->setValue(preset.h);
      sync_preview();
    });
    chip_layout->addWidget(chip, chip_index / 3, chip_index % 3);
    ++chip_index;
  }
  layout->addWidget(chip_host);

  auto *title_label = new QLabel(tr("Title"), body);
  title_label->setProperty("muted", true);
  layout->addWidget(title_label);
  auto *title_edit = new QLineEdit(body);
  title_edit->setMinimumHeight(44);
  // Default title: next free "Artwork N".
  int next_index = 1;
  {
    std::error_code ec;
    const auto dir = kreart::mobile::mobile_projects_dir();
    while (std::filesystem::exists(dir / ("Artwork " + std::to_string(next_index) + ".kra"), ec)) {
      ++next_index;
    }
  }
  title_edit->setText(QString("Artwork %1").arg(next_index));
  layout->addWidget(title_edit);

  auto *error = new QLabel(body);
  error->setStyleSheet("color: #F45C5C; font-size: 12px;");
  error->hide();
  layout->addWidget(error);

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);

  sheet->set_secondary_action(tr("Cancel"), [sheet]() { sheet->close(); });
  sheet->set_primary_action(tr("Create"), [this, sheet, width_spin, height_spin, title_edit, error]() {
    const QString title = title_edit->text().trimmed();
    QString message;
    if (!kreart::mobile::is_valid_file_stem(title, message)) {
      error->setText(message);
      error->show();
      return;
    }
    sheet->close();
    emit createNewProject(width_spin->value(), height_spin->value(), title);
  });

  connect(sheet, &MobileSheet::closed, this, [this, sheet]() {
    if (active_sheet_ == sheet) active_sheet_ = nullptr;
    sheet->deleteLater();
  });
  sheet->open();
}

} // namespace kreart::ui::mobile
