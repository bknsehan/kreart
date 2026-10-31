#include "kreart/ui/gallery_view.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/io/kra.h"
#include "kreart/io/png.h"
#include "kreart/ui/app_settings.h"
#include "kreart/core/document.h"
#include <QComboBox>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

namespace kreart::ui {
static QPixmap create_canvas_thumbnail_pixmap(const QPixmap &src_pix, int target_w = 214, int target_h = 134) {
  if (src_pix.isNull()) return {};

  const int margin = 8;
  QPixmap scaled_art = src_pix.scaled(target_w - margin * 2, target_h - margin * 2,
                                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const int art_w = scaled_art.width();
  const int art_h = scaled_art.height();

  QPixmap result(target_w, target_h);
  result.fill(Qt::transparent);

  QPainter p(&result);
  p.setRenderHint(QPainter::Antialiasing, true);

  const int ox = (target_w - art_w) / 2;
  const int oy = (target_h - art_h) / 2;
  const QRect art_rect(ox, oy, art_w, art_h);

  // 1. Subtle drop shadow
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 90));
  p.drawRoundedRect(art_rect.adjusted(1, 2, 2, 3), 3.0, 3.0);

  // 2. Background for transparent art
  const auto &s = AppSettings::instance();
  const bool is_dark = (s.theme() == ThemeMode::DARK ||
                       (s.theme() == ThemeMode::SYSTEM &&
                        QApplication::palette().color(QPalette::Window).value() < 128));
  const auto trans_mode = s.effective_transparency_mode(is_dark);

  p.save();
  p.setClipRect(art_rect);
  if (trans_mode == TransparencyMode::SOLID_COLOR) {
    p.fillRect(art_rect, s.transparency_solid_color());
  } else {
    const int check_sz = 8;
    const QColor c1 = (trans_mode == TransparencyMode::DARK_CHECKERBOARD)
                          ? QColor(56, 59, 66)
                          : QColor(255, 255, 255);
    const QColor c2 = (trans_mode == TransparencyMode::DARK_CHECKERBOARD)
                          ? QColor(41, 43, 48)
                          : QColor(230, 232, 236);
    for (int y = art_rect.top(); y < art_rect.bottom(); y += check_sz) {
      for (int x = art_rect.left(); x < art_rect.right(); x += check_sz) {
        const bool alt = ((x / check_sz) + (y / check_sz)) % 2 == 0;
        p.fillRect(QRect(x, y, check_sz, check_sz), alt ? c1 : c2);
      }
    }
  }

  // 3. Draw artwork
  p.drawPixmap(ox, oy, scaled_art);

  // 4. Subtle canvas border
  p.restore();
  p.setPen(QPen(QColor(0, 0, 0, 70), 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawRect(art_rect);

  p.end();
  return result;
}

class GalleryCard : public QWidget {
public:
  GalleryCard(const ProjectItem &item, std::function<void()> on_open, std::function<void()> on_refresh, QWidget *parent = nullptr)
      : QWidget(parent), item_(item), on_open_(std::move(on_open)), on_refresh_(std::move(on_refresh)) {
    setCursor(Qt::PointingHandCursor);
    setFixedSize(240, 220);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    // Thumbnail Preview Area
    thumb_label_ = new QLabel(this);
    thumb_label_->setAlignment(Qt::AlignCenter);
    thumb_label_->setFixedSize(220, 140);
    thumb_label_->setStyleSheet(
        "background-color: rgba(0, 0, 0, 0.35);"
        "border-radius: 8px;"
        "border: 1px solid rgba(255, 255, 255, 0.08);");

    QPixmap raw_pix;
    if (!item_.thumbnail_pixmap.isNull()) {
      raw_pix = item_.thumbnail_pixmap;
    } else if (!item_.thumbnail_path.empty() && std::filesystem::exists(item_.thumbnail_path)) {
      raw_pix.load(QString::fromStdString(item_.thumbnail_path.string()));
    }

    if (!raw_pix.isNull()) {
      thumb_label_->setPixmap(create_canvas_thumbnail_pixmap(raw_pix, 214, 134));
    } else {
      // Clean placeholder with Kreart icon and dimensions
      thumb_label_->setText(QString("%1 × %2").arg(item_.width).arg(item_.height));
      thumb_label_->setStyleSheet(
          "color: rgba(255, 255, 255, 0.4);"
          "background-color: rgba(255, 255, 255, 0.04);"
          "border-radius: 8px;"
          "font-size: 12px;"
          "font-weight: bold;"
          "border: 1px dashed rgba(255, 255, 255, 0.15);");
    }
    layout->addWidget(thumb_label_);
    // Info row: Title, dimensions, date, and menu button
    auto *info_row = new QHBoxLayout();
    info_row->setContentsMargins(2, 0, 2, 0);
    info_row->setSpacing(6);

    auto *text_box = new QVBoxLayout();
    text_box->setContentsMargins(0, 0, 0, 0);
    text_box->setSpacing(2);

    auto *title_lbl = new QLabel(item_.title, this);
    title_lbl->setStyleSheet("font-weight: 600; font-size: 13px;");
    text_box->addWidget(title_lbl);

    QString date_str = item_.last_modified.isValid() ? item_.last_modified.toString("MMM d, yyyy") : "";
    auto *sub_lbl = new QLabel(QString("%1 × %2 • %3").arg(item_.width).arg(item_.height).arg(date_str), this);
    sub_lbl->setStyleSheet("color: rgba(255, 255, 255, 0.5); font-size: 11px;");
    text_box->addWidget(sub_lbl);

    info_row->addLayout(text_box, 1);

    auto *menu_btn = new QPushButton("⋮", this);
    menu_btn->setFixedSize(26, 26);
    menu_btn->setStyleSheet(
        "QPushButton { border: none; font-size: 16px; font-weight: bold; color: gray; border-radius: 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); color: white; }");
    connect(menu_btn, &QPushButton::clicked, this, &GalleryCard::show_context_menu);
    info_row->addWidget(menu_btn);

    layout->addLayout(info_row);
  }

protected:
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && on_open_) {
      on_open_();
    }
  }
  void enterEvent(QEnterEvent *) override {
    hovered_ = true;
    update();
  }

  void leaveEvent(QEvent *) override {
    hovered_ = false;
    update();
  }

  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor cardBg = palette().color(QPalette::Window);
    cardBg.setAlpha(hovered_ ? 230 : 170);
    QColor border = hovered_ ? QColor(59, 130, 246, 220) : palette().color(QPalette::Mid);
    border.setAlpha(hovered_ ? 220 : 80);

    p.setPen(QPen(border, hovered_ ? 1.5 : 1.0));
    p.setBrush(cardBg);
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10.0, 10.0);
  }

private:
  void show_context_menu() {
    QMenu menu(this);
    auto *rename_act = menu.addAction(tr("Rename"));
    auto *dup_act = menu.addAction(tr("Duplicate"));
    auto *del_act = menu.addAction(tr("Delete"));

    auto *chosen = menu.exec(QCursor::pos());
    auto refresh_fn = on_refresh_;
    if (chosen == rename_act) {
      bool ok = false;
      QString new_name = QInputDialog::getText(this, tr("Rename Project"), tr("New title:"),
                                               QLineEdit::Normal, item_.title, &ok);
      if (ok && !new_name.isEmpty() && new_name != item_.title) {
        std::filesystem::path new_path = item_.file_path.parent_path() / (new_name.toStdString() + ".kra");
        std::error_code ec;
        std::filesystem::rename(item_.file_path, new_path, ec);
        if (!item_.thumbnail_path.empty() && std::filesystem::exists(item_.thumbnail_path)) {
          std::filesystem::path new_thumb = item_.file_path.parent_path() / (new_name.toStdString() + ".thumb.png");
          std::filesystem::rename(item_.thumbnail_path, new_thumb, ec);
        }
        if (refresh_fn) {
          QTimer::singleShot(0, refresh_fn);
        }
      }
    } else if (chosen == dup_act) {
      std::string dup_name = item_.title.toStdString() + " Copy";
      std::filesystem::path dup_path = item_.file_path.parent_path() / (dup_name + ".kra");
      std::error_code ec;
      std::filesystem::copy_file(item_.file_path, dup_path, ec);
      if (!item_.thumbnail_path.empty() && std::filesystem::exists(item_.thumbnail_path)) {
        std::filesystem::path dup_thumb = item_.file_path.parent_path() / (dup_name + ".thumb.png");
        std::filesystem::copy_file(item_.thumbnail_path, dup_thumb, ec);
      }
      if (refresh_fn) {
        QTimer::singleShot(0, refresh_fn);
      }
    } else if (chosen == del_act) {
      auto ans = QMessageBox::question(this, tr("Delete Project"),
                                       tr("Are you sure you want to delete '%1'?").arg(item_.title),
                                       QMessageBox::Yes | QMessageBox::No);
      if (ans == QMessageBox::Yes) {
        std::error_code ec;
        std::filesystem::remove(item_.file_path, ec);
        if (!item_.thumbnail_path.empty()) std::filesystem::remove(item_.thumbnail_path, ec);
        if (refresh_fn) {
          QTimer::singleShot(0, refresh_fn);
        }
      }
    }
  }

  ProjectItem item_;
  QLabel *thumb_label_ = nullptr;
  std::function<void()> on_open_;
  std::function<void()> on_refresh_;
  bool hovered_ = false;
};

// Dynamic Canvas Aspect Ratio Preview Widget
// ----------------------------------------------------------------------------
class CanvasRatioPreview : public QWidget {
public:
  explicit CanvasRatioPreview(QWidget *parent = nullptr) : QWidget(parent) {
    setFixedHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void set_dimensions(int w, int h) {
    if (width_ != w || height_ != h) {
      width_ = std::max(1, w);
      height_ = std::max(1, h);
      update();
    }
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Background container
    p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    QColor bg = palette().color(QPalette::Window);
    bg.setAlpha(120);
    p.setBrush(bg);
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8.0, 8.0);

    const int margin_x = 28;
    const int margin_y = 16;
    const int avail_w = width() - margin_x * 2;
    const int avail_h = height() - margin_y * 2 - 24;
    if (avail_w <= 0 || avail_h <= 0) return;

    const double aspect = static_cast<double>(width_) / height_;
    int c_w = 0;
    int c_h = 0;
    if (aspect >= static_cast<double>(avail_w) / avail_h) {
      c_w = avail_w;
      c_h = std::max(10, static_cast<int>(avail_w / aspect));
    } else {
      c_h = avail_h;
      c_w = std::max(10, static_cast<int>(avail_h * aspect));
    }

    const int ox = (width() - c_w) / 2;
    const int oy = margin_y + (avail_h - c_h) / 2;
    const QRect canvas_rect(ox, oy, c_w, c_h);

    // Drop shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawRoundedRect(canvas_rect.adjusted(2, 3, 2, 3), 4.0, 4.0);

    // Canvas sheet
    p.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
    p.setBrush(QColor(248, 249, 250));
    p.drawRoundedRect(canvas_rect, 3.0, 3.0);

    // Orientation & aspect ratio label
    p.setPen(palette().color(QPalette::Text));
    QFont font = p.font();
    font.setPointSize(10);
    font.setBold(true);
    p.setFont(font);

    QString orient_str;
    if (width_ > height_) orient_str = tr("Landscape");
    else if (height_ > width_) orient_str = tr("Portrait");
    else orient_str = tr("Square");

    auto gcd = [](int a, int b) {
      while (b != 0) { int t = b; b = a % b; a = t; }
      return a;
    };
    int g = gcd(width_, height_);
    int rw = width_ / g;
    int rh = height_ / g;
    QString ratio_str = QString("%1:%2").arg(rw).arg(rh);
    if (rw > 32 || rh > 32) {
      ratio_str = QString::number(aspect, 'f', 2) + ":1";
    }

    QString label_text = QString("%1 × %2 px  •  %3 (%4)")
                             .arg(width_).arg(height_).arg(orient_str).arg(ratio_str);
    p.drawText(QRect(0, height() - 26, width(), 22), Qt::AlignCenter, label_text);
  }

private:
  int width_ = 1920;
  int height_ = 1080;
};

// ----------------------------------------------------------------------------
// New Canvas Dialog with live preview, presets, swap & OK button
// ----------------------------------------------------------------------------
class NewCanvasDialog : public QDialog {
public:
  struct PresetItem {
    QString name;
    int w;
    int h;
  };

  explicit NewCanvasDialog(QWidget *parent = nullptr) : QDialog(parent) {
    setWindowTitle(tr("New Canvas"));
    setModal(true);
    setFixedWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    // 1. Live Aspect Ratio Preview
    preview_ = new CanvasRatioPreview(this);
    preview_->set_dimensions(selected_w_, selected_h_);
    layout->addWidget(preview_);

    // 2. Presets header
    auto *preset_row = new QHBoxLayout();
    auto *preset_header = new QLabel(tr("Presets:"), this);
    preset_header->setStyleSheet("font-size: 13px; font-weight: bold;");
    preset_row->addWidget(preset_header);

    custom_badge_ = new QLabel(tr("Preset"), this);
    custom_badge_->setMinimumWidth(80);
    custom_badge_->setStyleSheet("color: #3b82f6; font-size: 11px; font-weight: bold;");
    preset_row->addWidget(custom_badge_);
    preset_row->addStretch(1);
    layout->addLayout(preset_row);

    presets_ = {
        {tr("Full HD Landscape"), 1920, 1080},
        {tr("Full HD Portrait"), 1080, 1920},
        {tr("Square (Post / Avatar)"), 2048, 2048},
        {tr("4K UHD Landscape"), 3840, 2160},
        {tr("A4 Illustration (300 DPI)"), 2480, 3508},
    };

    for (const auto &p : presets_) {
      auto *btn = new QPushButton(QString("%1  (%2 × %3)").arg(p.name).arg(p.w).arg(p.h), this);
      btn->setFixedHeight(34);
      connect(btn, &QPushButton::clicked, this, [this, w = p.w, h = p.h]() {
        w_spin_->setValue(w);
        h_spin_->setValue(h);
        preview_->set_dimensions(w, h);
        update_preset_highlights();
      });
      layout->addWidget(btn);
      preset_buttons_.push_back(btn);
    }

    // 3. Width, Height and Swap button
    auto *dims_group = new QWidget(this);
    auto *d_layout = new QHBoxLayout(dims_group);
    d_layout->setContentsMargins(0, 4, 0, 0);
    d_layout->setSpacing(8);

    d_layout->addWidget(new QLabel(tr("W:"), this));
    w_spin_ = new QSpinBox(this);
    w_spin_->setRange(64, 8192);
    w_spin_->setValue(selected_w_);
    d_layout->addWidget(w_spin_, 1);

    auto *swap_btn = new QPushButton("⇄", this);
    swap_btn->setFixedSize(36, 32);
    swap_btn->setToolTip(tr("Swap Width and Height (Rotate Orientation)"));
    swap_btn->setStyleSheet(
        "QPushButton {"
        "  font-size: 16px; font-weight: bold; border-radius: 6px;"
        "  background-color: rgba(255, 255, 255, 0.08); border: 1px solid rgba(255, 255, 255, 0.15);"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.18); }");
    connect(swap_btn, &QPushButton::clicked, this, [this]() {
      const int cur_w = w_spin_->value();
      const int cur_h = h_spin_->value();
      w_spin_->setValue(cur_h);
      h_spin_->setValue(cur_w);
    });
    d_layout->addWidget(swap_btn);

    d_layout->addWidget(new QLabel(tr("H:"), this));
    h_spin_ = new QSpinBox(this);
    h_spin_->setRange(64, 8192);
    h_spin_->setValue(selected_h_);
    d_layout->addWidget(h_spin_, 1);

    connect(w_spin_, &QSpinBox::valueChanged, this, [this](int) {
      selected_w_ = w_spin_->value();
      selected_h_ = h_spin_->value();
      preview_->set_dimensions(selected_w_, selected_h_);
      update_preset_highlights();
    });
    connect(h_spin_, &QSpinBox::valueChanged, this, [this](int) {
      selected_w_ = w_spin_->value();
      selected_h_ = h_spin_->value();
      preview_->set_dimensions(selected_w_, selected_h_);
      update_preset_highlights();
    });

    layout->addWidget(dims_group);

    // 4. Action Buttons (Cancel / Okay)
    auto *btn_row = new QHBoxLayout();
    btn_row->addStretch(1);

    auto *cancel_btn = new QPushButton(tr("Cancel"), this);
    cancel_btn->setFixedHeight(36);
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    btn_row->addWidget(cancel_btn);

    auto *ok_btn = new QPushButton(tr("Okay"), this);
    ok_btn->setFixedHeight(36);
    ok_btn->setDefault(true);
    const auto *pal = AppSettings::accent_from_id(AppSettings::instance().accent().toStdString());
    const QString acc_m = pal ? QString::fromUtf8(pal->dark_accent) : QString("#3b82f6");
    const QString acc_h = pal ? QString::fromUtf8(pal->dark_hover) : QString("#2563eb");
    const QString acc_p = pal ? QString::fromUtf8(pal->dark_active_bg) : QString("#1d4ed8");
    ok_btn->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: %1; color: white; font-weight: bold;"
        "  padding: 0 22px; border-radius: 6px; border: 1px solid %2;"
        "}"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }").arg(acc_m, acc_h, acc_p));
    connect(ok_btn, &QPushButton::clicked, this, [this]() {
      selected_w_ = w_spin_->value();
      selected_h_ = h_spin_->value();
      accept();
    });
    btn_row->addWidget(ok_btn);

    layout->addLayout(btn_row);
    update_preset_highlights();
  }

  int width() const { return selected_w_; }
  int height() const { return selected_h_; }

private:
  void update_preset_highlights() {
    const int w = w_spin_->value();
    const int h = h_spin_->value();
    bool matched = false;

    for (std::size_t i = 0; i < presets_.size(); ++i) {
      QPushButton *btn = preset_buttons_[i];
      if (presets_[i].w == w && presets_[i].h == h) {
        matched = true;
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(59, 130, 246, 0.28);"
            "  border: 1.5px solid #3b82f6;"
            "  border-radius: 6px;"
            "  font-weight: bold;"
            "  text-align: left;"
            "  padding-left: 12px;"
            "  color: palette(text);"
            "}");
      } else {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(255, 255, 255, 0.05);"
            "  border: 1px solid rgba(255, 255, 255, 0.1);"
            "  border-radius: 6px;"
            "  text-align: left;"
            "  padding-left: 12px;"
            "  color: palette(text);"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(255, 255, 255, 0.1);"
            "}");
      }
    }

    if (custom_badge_) {
      custom_badge_->setText(matched ? tr("Preset") : tr("Custom"));
      custom_badge_->setStyleSheet(matched ? "color: #3b82f6; font-size: 11px; font-weight: bold;"
                                           : "color: #f59e0b; font-size: 11px; font-weight: bold;");
    }
  }

  int selected_w_ = 1920;
  int selected_h_ = 1080;
  CanvasRatioPreview *preview_ = nullptr;
  QLabel *custom_badge_ = nullptr;
  std::vector<PresetItem> presets_;
  std::vector<QPushButton *> preset_buttons_;
  QSpinBox *w_spin_ = nullptr;
  QSpinBox *h_spin_ = nullptr;
};

// ----------------------------------------------------------------------------
// GalleryView Implementation
// ----------------------------------------------------------------------------
std::filesystem::path GalleryView::projects_directory() {
  QString data_path = AppSettings::instance().projects_directory();
  std::filesystem::path dir(data_path.toStdString());
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

GalleryView::GalleryView(QWidget *parent)
    : QWidget(parent) {
  build_ui();
  refresh_projects();
  connect(&AppSettings::instance(), &AppSettings::projectsDirectoryChanged, this, [this](const QString &) {
    refresh_projects();
  });
  connect(&AppSettings::instance(), &AppSettings::transparencySettingsChanged, this, [this]() {
    rebuild_grid();
  });
}

void GalleryView::build_ui() {
  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(16, 12, 16, 16);
  root_layout->setSpacing(12);

  // Top Bar (App Title / Logo + Action buttons)
  auto *top_bar = new QHBoxLayout();
  top_bar->setSpacing(10);

  auto *logo_lbl = new QLabel(this);
  logo_lbl->setPixmap(IconHelper::load_symbolic(":/icons/icons/kreart-icon.svg", palette(), 32).pixmap(32, 32));
  top_bar->addWidget(logo_lbl);

  auto *title_lbl = new QLabel(tr("Kreart Gallery"), this);
  title_lbl->setStyleSheet("font-size: 20px; font-weight: bold;");
  top_bar->addWidget(title_lbl);

  top_bar->addStretch(1);

  auto *startup_combo = new QComboBox(this);
  startup_combo->setFixedHeight(36);
  startup_combo->addItem(tr("On Startup: Gallery"), static_cast<int>(StartupMode::GALLERY));
  startup_combo->addItem(tr("On Startup: Blank Canvas"), static_cast<int>(StartupMode::BLANK_CANVAS));
  startup_combo->setCurrentIndex(AppSettings::instance().startup_mode() == StartupMode::BLANK_CANVAS ? 1 : 0);
  connect(startup_combo, &QComboBox::currentIndexChanged, this, [](int idx) {
    AppSettings::instance().set_startup_mode(idx == 1 ? StartupMode::BLANK_CANVAS : StartupMode::GALLERY);
  });
  top_bar->addWidget(startup_combo);

  auto *folder_btn = new QPushButton(tr("Set Folder..."), this);
  folder_btn->setFixedHeight(36);
  folder_btn->setToolTip(tr("Choose where your projects are saved"));
  connect(folder_btn, &QPushButton::clicked, this, [this]() {
    const QString current = AppSettings::instance().projects_directory();
    const QString selected = QFileDialog::getExistingDirectory(this, tr("Select Projects Folder"), current);
    if (!selected.isEmpty() && selected != current) {
      AppSettings::instance().set_projects_directory(selected);
      refresh_projects();
    }
  });
  top_bar->addWidget(folder_btn);
  auto *import_btn = new QPushButton(tr("Import"), this);
  import_btn->setIcon(IconHelper::load_symbolic(":/icons/icons/bundle-import-symbolic.svg", palette(), 18));
  import_btn->setFixedHeight(36);
  connect(import_btn, &QPushButton::clicked, this, &GalleryView::importProjectRequested);
  top_bar->addWidget(import_btn);

  auto *blank_btn = new QPushButton(tr("Blank Canvas"), this);
  blank_btn->setFixedHeight(36);
  blank_btn->setToolTip(tr("Open the editor with a blank canvas directly"));
  connect(blank_btn, &QPushButton::clicked, this, &GalleryView::openBlankCanvasRequested);
  top_bar->addWidget(blank_btn);

  auto *new_btn = new QPushButton(tr("+ New Canvas"), this);
  new_btn->setFixedHeight(36);
  const auto *pal_new = AppSettings::accent_from_id(AppSettings::instance().accent().toStdString());
  const QString new_acc_m = pal_new ? QString::fromUtf8(pal_new->dark_accent) : QString("#3b82f6");
  const QString new_acc_h = pal_new ? QString::fromUtf8(pal_new->dark_hover) : QString("#2563eb");
  const QString new_acc_p = pal_new ? QString::fromUtf8(pal_new->dark_active_bg) : QString("#1d4ed8");
  new_btn->setStyleSheet(QString(
      "QPushButton {"
      "  background-color: %1;"
      "  color: white;"
      "  font-weight: bold;"
      "  padding: 0 16px;"
      "  border-radius: 6px;"
      "  border: 1px solid %2;"
      "}"
      "QPushButton:hover { background-color: %2; }"
      "QPushButton:pressed { background-color: %3; }").arg(new_acc_m, new_acc_h, new_acc_p));
  connect(new_btn, &QPushButton::clicked, this, &GalleryView::on_new_canvas_clicked);
  top_bar->addWidget(new_btn);
  connect(&AppSettings::instance(), &AppSettings::startupModeChanged, this, [startup_combo](StartupMode mode) {
    startup_combo->blockSignals(true);
    startup_combo->setCurrentIndex(mode == StartupMode::BLANK_CANVAS ? 1 : 0);
    startup_combo->blockSignals(false);
  });

  root_layout->addLayout(top_bar);

  // Scroll Area with dynamic Grid
  scroll_area_ = new QScrollArea(this);
  scroll_area_->setWidgetResizable(true);
  scroll_area_->setFrameShape(QFrame::NoFrame);

  grid_container_ = new QWidget(scroll_area_);
  grid_layout_ = new QGridLayout(grid_container_);
  grid_layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  grid_layout_->setContentsMargins(16, 16, 16, 16);
  grid_layout_->setSpacing(18);
  scroll_area_->setWidget(grid_container_);

  root_layout->addWidget(scroll_area_, 1);

  // Centered Empty State Container
  empty_container_ = new QWidget(this);
  auto *empty_layout = new QVBoxLayout(empty_container_);
  empty_layout->setAlignment(Qt::AlignCenter);
  empty_layout->setSpacing(12);

  auto *empty_icon = new QLabel(empty_container_);
  empty_icon->setPixmap(IconHelper::load_symbolic(":/icons/icons/kreart-icon.svg", palette(), 48).pixmap(48, 48));
  empty_icon->setAlignment(Qt::AlignCenter);
  empty_layout->addWidget(empty_icon);

  empty_label_ = new QLabel(tr("No projects yet\nClick '+ New Canvas' or 'Blank Canvas' above to start your first artwork!"), empty_container_);
  empty_label_->setAlignment(Qt::AlignCenter);
  empty_label_->setStyleSheet("color: gray; font-size: 15px; line-height: 1.6; font-weight: 500;");
  empty_layout->addWidget(empty_label_);

  empty_container_->setVisible(false);
  root_layout->addWidget(empty_container_, 1);
}

void GalleryView::on_new_canvas_clicked() {
  NewCanvasDialog dlg(this);
  if (dlg.exec() == QDialog::Accepted) {
    const int w = dlg.width();
    const int h = dlg.height();
    QString title = tr("Artwork %1").arg(projects_.size() + 1);
    emit createNewProject(w, h, title);
  }
}

void GalleryView::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  const int w = event->size().width();
  const int card_w = 240;
  const int spacing = 18;
  const int margins = 48;
  const int new_cols = std::clamp((w - margins) / (card_w + spacing), 1, 8);
  if (new_cols != columns_) {
    columns_ = new_cols;
    rebuild_grid();
  }
}

void GalleryView::refresh_projects() {
  projects_.clear();
  const auto dir = projects_directory();
  std::error_code ec;

  if (std::filesystem::is_directory(dir, ec)) {
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() == ".kra") {
        ProjectItem item;
        item.file_path = entry.path();
        item.title = QString::fromStdString(entry.path().stem().string());

        // 1. Check for existing sidecar thumbnail (.thumb.png)
        std::filesystem::path thumb = entry.path().parent_path() / (entry.path().stem().string() + ".thumb.png");
        bool thumb_valid = false;
        if (std::filesystem::exists(thumb, ec)) {
          QImage thumb_img(QString::fromStdString(thumb.string()));
          if (!thumb_img.isNull()) {
            item.thumbnail_path = thumb;
            item.thumbnail_pixmap = QPixmap::fromImage(thumb_img);
            thumb_valid = true;
          }
        }

        // 2. Parse KRA thumbnail & actual width/height from maindoc.xml
        std::vector<std::uint8_t> png_bytes;
        std::string err;
        int kra_w = 0, kra_h = 0;
        if (io::read_kra_thumbnail(entry.path(), png_bytes, kra_w, kra_h, err, false)) {
          if (kra_w > 0 && kra_h > 0) {
            item.width = kra_w;
            item.height = kra_h;
          }
          if (!thumb_valid && !png_bytes.empty()) {
            QImage kra_thumb_img;
            kra_thumb_img.loadFromData(png_bytes.data(), static_cast<uint>(png_bytes.size()));
            if (!kra_thumb_img.isNull()) {
              QFile out_file(QString::fromStdString(thumb.string()));
              if (out_file.open(QIODevice::WriteOnly)) {
                out_file.write(reinterpret_cast<const char *>(png_bytes.data()), static_cast<qint64>(png_bytes.size()));
                out_file.close();
                item.thumbnail_path = thumb;
              }
              item.thumbnail_pixmap = QPixmap::fromImage(kra_thumb_img);
              thumb_valid = true;
            }
          }
        }

        auto ftime = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec) {
          auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
              ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
          std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
          item.last_modified = QDateTime::fromSecsSinceEpoch(tt);
        }

        projects_.push_back(item);
      }
    }
  }

  // Sort newest first
  std::sort(projects_.begin(), projects_.end(), [](const ProjectItem &a, const ProjectItem &b) {
    return a.last_modified > b.last_modified;
  });

  rebuild_grid();
}

void GalleryView::rebuild_grid() {
  QLayoutItem *child;
  while ((child = grid_layout_->takeAt(0)) != nullptr) {
    delete child->widget();
    delete child;
  }

  if (projects_.empty()) {
    empty_container_->setVisible(true);
    scroll_area_->setVisible(false);
    return;
  }

  empty_container_->setVisible(false);
  scroll_area_->setVisible(true);
  grid_container_->setVisible(true);
  for (std::size_t i = 0; i < projects_.size(); ++i) {
    const auto &item = projects_[i];
    auto *card = new GalleryCard(
        item,
        [this, path = item.file_path]() { emit openProject(path); },
        [this]() { refresh_projects(); },
        grid_container_);

    int row = static_cast<int>(i) / columns_;
    int col = static_cast<int>(i) % columns_;
    grid_layout_->addWidget(card, row, col);
  }
}

} // namespace kreart::ui
