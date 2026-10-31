#include "kreart/ui/android_window.h"

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <filesystem>

#include "kreart/mobile/android_utils.h"
#include "kreart/ui/app_settings.h"
#include "kreart/ui/mobile/mobile_theme.h"

namespace kreart::ui {

AndroidAppWindow::AndroidAppWindow(QWidget *parent) : QWidget(parent) {
  setWindowTitle(tr("Kreart"));
  setObjectName("AndroidAppWindow");

  // Ink & Amber stylesheet, scoped to QWidget#MobileSheet so desktop is untouched.
  if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
    app->setStyleSheet(mobile::sheet_qss());
  }

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  stack_ = new QStackedWidget(this);

  gallery_view_ = new mobile::MobileGallery(this);
  stack_->addWidget(gallery_view_);

  editor_view_ = new MobileEditorView(this);
  stack_->addWidget(editor_view_);

  layout->addWidget(stack_);
  setup_connections();

  qApp->installEventFilter(this);
}

AndroidAppWindow::~AndroidAppWindow() {
  if (qApp) qApp->removeEventFilter(this);
}

void AndroidAppWindow::setup_connections() {
  // Gallery -> Editor
  connect(gallery_view_, &mobile::MobileGallery::openProject, this,
          [this](const std::filesystem::path &path) {
            editor_view_->load_document(path);
            go_to_editor();
          });

  connect(gallery_view_, &mobile::MobileGallery::createNewProject, this,
          [this](int w, int h, const QString &title) {
            editor_view_->create_new_document(w, h, title);
            go_to_editor();
          });

  connect(gallery_view_, &mobile::MobileGallery::openBlankCanvasRequested, this, [this]() {
    editor_view_->create_new_document(1920, 1080, tr("Untitled"));
    go_to_editor();
  });

  connect(gallery_view_, &mobile::MobileGallery::importProjectRequested, this, [this]() {
    const QString picked = kreart::mobile::pick_file(
        this, QStringLiteral("application/x-krita,image/png"), tr("Import Project"));
    if (!picked.isEmpty()) {
      open_file(picked.toStdString());
      return;
    }
    // Desktop `-m` preview (or a provider that cannot expose a path).
    const QString filename = QFileDialog::getOpenFileName(
        this, tr("Import Project"), QString(), tr("Krita or Image (*.kra *.png)"));
    if (!filename.isEmpty()) {
      open_file(filename.toStdString());
    }
  });

  // Editor -> Gallery
  connect(editor_view_, &MobileEditorView::backToGalleryRequested, this, [this]() {
    go_to_gallery();
  });
}

void AndroidAppWindow::go_to_editor() {
  stack_->setCurrentWidget(editor_view_);
  editor_view_->setFocus(Qt::OtherFocusReason);
}

void AndroidAppWindow::go_to_gallery() {
  gallery_view_->refresh_projects();
  stack_->setCurrentIndex(0);
  gallery_view_->setFocus(Qt::OtherFocusReason);
}

void AndroidAppWindow::open_file(const std::filesystem::path &path) {
  editor_view_->load_document(path);
  go_to_editor();
}

void AndroidAppWindow::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (startup_applied_) return;
  startup_applied_ = true;

  // Honour the startup preference once, on first show.
  if (AppSettings::instance().startup_mode() == StartupMode::BLANK_CANVAS) {
    editor_view_->create_new_document(1920, 1080, tr("Untitled"));
    go_to_editor();
  }
}

bool AndroidAppWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() != QEvent::KeyPress) {
    return QWidget::eventFilter(obj, event);
  }
  auto *key_event = static_cast<QKeyEvent *>(event);
  if (key_event->key() != Qt::Key_Back) {
    return QWidget::eventFilter(obj, event);
  }

  // Canvas::keyPressEvent swallows unhandled keys while focused, so Back must be
  // handled here at application level.
  if (stack_->currentWidget() == editor_view_) {
    if (editor_view_->close_topmost_overlay()) {
      return true;
    }
    editor_view_->request_back_to_gallery();
    return true;
  }
  if (stack_->currentIndex() == 0 && gallery_view_) {
    qApp->quit();
    return true;
  }
  return true;
}

void AndroidAppWindow::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
    if (stack_->currentWidget() == editor_view_) {
      if (editor_view_->close_topmost_overlay()) {
        event->accept();
        return;
      }
      editor_view_->request_back_to_gallery();
      event->accept();
      return;
    }
    qApp->quit();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

} // namespace kreart::ui
