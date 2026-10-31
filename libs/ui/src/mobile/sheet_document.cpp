#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <filesystem>

#include "kreart/io/autosave.h"
#include "kreart/mobile/android_utils.h"
#include "kreart/ui/timelapse_dialog.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

QPushButton *make_row(QWidget *parent, const QString &label, std::function<void()> fn,
                      bool accent = false) {
  auto *btn = new QPushButton(label, parent);
  btn->setMinimumHeight(52);
  btn->setCursor(Qt::PointingHandCursor);
  if (accent) {
    btn->setProperty("accent", true);
  }
  QObject::connect(btn, &QPushButton::clicked, parent, [fn]() { if (fn) fn(); });
  return btn;
}

} // namespace

void build_document_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  core::Document *doc = ctx.doc;

  // Document info
  if (doc) {
    auto *info = new QLabel(
        QObject::tr("%1 x %2 px  -  %3 layers")
            .arg(doc->width())
            .arg(doc->height())
            .arg(doc->layer_count()),
        body);
    info->setProperty("muted", true);
    layout->addWidget(info);
  }

  layout->addWidget(make_row(body, QObject::tr("Save"), [&ctx]() {
    if (ctx.save_document) ctx.save_document();
  }, true));

  layout->addWidget(make_row(body, QObject::tr("Save As..."), [&ctx, &sheets, doc]() {
    const QString initial = QObject::tr("Artwork");
    auto *text_sheet = sheets.make_text_sheet(
        QObject::tr("Save As"), QObject::tr("File name"),
        QString::fromStdString(kreart::mobile::mobile_projects_dir().filename().string()).isEmpty()
            ? initial
            : initial,
        [&ctx, doc](const QString &text, QString &error) {
          QString name = text.trimmed();
          if (name.isEmpty()) {
            error = QObject::tr("Name cannot be empty");
            return false;
          }
          if (!name.endsWith(".kra", Qt::CaseInsensitive)) name += ".kra";
          if (!kreart::mobile::is_valid_file_stem(name.mid(0, name.size() - 4), error)) return false;
          const std::filesystem::path target = kreart::mobile::mobile_projects_dir() / name.toStdString();
          if (!ctx.save_document_as) return false;
          if (!ctx.save_document_as(target)) {
            error = QObject::tr("Could not save the document");
            return false;
          }
          (void)doc;
          return true;
        });
    sheets.show_sheet_public(text_sheet);
  }));

  layout->addWidget(make_row(body, QObject::tr("Export PNG"), [&ctx]() {
    if (!ctx.export_png) return;
    auto base = kreart::mobile::mobile_projects_dir() / "export.png";
    if (!ctx.export_png(base)) return;
  }));
  layout->addWidget(make_row(body, QObject::tr("Timelapse Replay & Export..."), [&ctx, sheet]() {
    if (ctx.canvas) {
      ctx.canvas->timelapse_recorder().flush_pending_frames();
      TimelapseDialog dlg(ctx.canvas->timelapse_recorder(), ctx.canvas->tracker(), sheet);
      dlg.exec();
    }
  }));

  layout->addWidget(make_row(body, QObject::tr("Import Image as Layer"), [&ctx, &sheets]() {
    const QString path = kreart::mobile::pick_file(sheets.host(), QStringLiteral("image/*"),
                                           QObject::tr("Import Image"));
    if (path.isEmpty()) return;
    if (ctx.import_image_as_layer &&
        ctx.import_image_as_layer(std::filesystem::path(path.toStdString()))) {
      if (ctx.document_structure_changed) ctx.document_structure_changed();
    }
  }));

  layout->addWidget(make_row(body, QObject::tr("Import Brush Pack"), [&ctx, &sheets]() {
    const QString path = kreart::mobile::pick_file(
        sheets.host(), QStringLiteral("application/octet-stream"),
        QObject::tr("Import Brush Pack"));
    if (path.isEmpty()) return;
    const std::filesystem::path source(path.toStdString());
    const std::string ext = source.extension().string();
    std::filesystem::path destination;
    if (ext == ".bundle") {
      destination = resources::ResourceDatabase::user_resource_directory() / "bundles";
    } else {
      destination = resources::ResourceDatabase::user_resource_directory() / "presets";
    }
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    const auto target = destination / source.filename();
    std::filesystem::copy_file(source, target,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      if (ctx.hud) ctx.hud(QObject::tr("Import failed"));
      return;
    }
    if (ctx.reload_presets) ctx.reload_presets();
    if (ctx.hud) ctx.hud(QObject::tr("Brush pack imported"));
  }));

  layout->addStretch(1);

  layout->addWidget(make_row(body, QObject::tr("Close and return to gallery"), [&ctx]() {
    if (ctx.close_to_gallery) ctx.close_to_gallery();
  }));

  sheet->set_body(body);
}

} // namespace kreart::ui::mobile
