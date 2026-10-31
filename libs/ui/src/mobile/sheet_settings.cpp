#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "kreart/mobile/android_utils.h"
#include "kreart/ui/app_settings.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

QString version_string() {
  return QStringLiteral("0.2.0");
}

} // namespace

void build_settings_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto &settings = AppSettings::instance();

  // Theme
  auto *theme_label = new QLabel(QObject::tr("Theme"), body);
  theme_label->setFont(font_section());
  layout->addWidget(theme_label);
  auto *theme_combo = new QComboBox(body);
  theme_combo->addItems({QObject::tr("Follow system"), QObject::tr("Dark"), QObject::tr("Light")});
  switch (settings.theme()) {
    case ThemeMode::SYSTEM: theme_combo->setCurrentIndex(0); break;
    case ThemeMode::DARK: theme_combo->setCurrentIndex(1); break;
    case ThemeMode::LIGHT: theme_combo->setCurrentIndex(2); break;
  }
  layout->addWidget(theme_combo);
  QObject::connect(theme_combo, &QComboBox::currentIndexChanged, body, [](int idx) {
    ThemeMode mode = ThemeMode::SYSTEM;
    if (idx == 1) mode = ThemeMode::DARK;
    else if (idx == 2) mode = ThemeMode::LIGHT;
    AppSettings::instance().set_theme(mode);
  });

  // Accent
  auto *accent_label = new QLabel(QObject::tr("Accent"), body);
  accent_label->setFont(font_section());
  layout->addWidget(accent_label);
  auto *accent_host = new QWidget(body);
  auto *accent_row = new QHBoxLayout(accent_host);
  accent_row->setContentsMargins(0, 0, 0, 0);
  accent_row->setSpacing(8);
  for (const auto &palette : AppSettings::accent_palettes()) {
    const QColor color(QString::fromUtf8(palette.dark_accent));
    auto *swatch = new QPushButton(accent_host);
    swatch->setFixedSize(44, 44);
    swatch->setCursor(Qt::PointingHandCursor);
    swatch->setToolTip(QString::fromUtf8(palette.label));
    swatch->setStyleSheet(
        QString("background-color: %1; border: 2px solid rgba(255,255,255,0.25);"
                " border-radius: 12px;")
            .arg(color.name()));
    const QString id = QString::fromUtf8(palette.id);
    QObject::connect(swatch, &QPushButton::clicked, accent_host, [id, color, sheet]() {
      AppSettings::instance().set_accent(id);
      // Repaint the mobile tokens live so the chrome picks the accent up.
      set_accent_colors(color, color.darker(120), QColor(26, 20, 8));
      if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setStyleSheet(sheet_qss());
      }
      sheet->update();
    });
    accent_row->addWidget(swatch);
  }
  accent_row->addStretch(1);
  layout->addWidget(accent_host);

  // Startup mode
  auto *startup_label = new QLabel(QObject::tr("Startup"), body);
  startup_label->setFont(font_section());
  layout->addWidget(startup_label);
  auto *startup_combo = new QComboBox(body);
  startup_combo->addItems({QObject::tr("Gallery"), QObject::tr("Blank canvas")});
  startup_combo->setCurrentIndex(settings.startup_mode() == StartupMode::BLANK_CANVAS ? 1 : 0);
  layout->addWidget(startup_combo);
  QObject::connect(startup_combo, &QComboBox::currentIndexChanged, body, [](int idx) {
    AppSettings::instance().set_startup_mode(idx == 1 ? StartupMode::BLANK_CANVAS
                                                      : StartupMode::GALLERY);
  });

  // Projects folder
  auto *folder_label = new QLabel(QObject::tr("Projects folder"), body);
  folder_label->setFont(font_section());
  layout->addWidget(folder_label);
  auto *folder_value = new QLabel(
      QString::fromStdString(kreart::mobile::mobile_projects_dir().string()), body);
  folder_value->setProperty("muted", true);
  folder_value->setWordWrap(true);
  layout->addWidget(folder_value);

  auto *change_folder = new QPushButton(QObject::tr("Change folder"), body);
  change_folder->setMinimumHeight(44);
  layout->addWidget(change_folder);
  QObject::connect(change_folder, &QPushButton::clicked, body, [&ctx, folder_value]() {
    const QString picked = kreart::mobile::pick_folder(nullptr);
    if (picked.isEmpty()) {
      if (ctx.hud) ctx.hud(QObject::tr("No folder selected"));
      return;
    }
    AppSettings::instance().set_projects_directory(picked);
    folder_value->setText(QString::fromStdString(kreart::mobile::mobile_projects_dir().string()));
  });

  // Autosave note
  auto *autosave_label = new QLabel(
      QObject::tr("Autosave runs every 60 seconds while the document has unsaved changes."),
      body);
  autosave_label->setProperty("muted", true);
  autosave_label->setWordWrap(true);
  layout->addWidget(autosave_label);

  // About
  auto *about_label = new QLabel(QObject::tr("About"), body);
  about_label->setFont(font_section());
  layout->addWidget(about_label);
  auto *about = new QLabel(QObject::tr("Kreart %1\nFast, native digital painting.").arg(version_string()),
                           body);
  about->setProperty("muted", true);
  about->setWordWrap(true);
  layout->addWidget(about);

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);
  (void)sheets;
}

} // namespace kreart::ui::mobile
