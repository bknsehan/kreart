#include "kreart/ui/project_gallery_window.h"
#include "kreart/ui/main_window.h"
#include "kreart/ui/app_settings.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/io/kra.h"
#include "kreart/io/png.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QCloseEvent>

namespace kreart::ui {

ProjectGalleryWindow::ProjectGalleryWindow(MainWindow *main_window, QWidget *parent)
    : QMainWindow(parent), main_window_(main_window) {
  setWindowTitle(tr("Kreart - Project Gallery"));
  setWindowIcon(QIcon(":/icons/icons/io.github.kreart.Kreart.svg"));
  resize(1020, 680);

  gallery_view_ = new GalleryView(this);
  setCentralWidget(gallery_view_);

  connect(gallery_view_, &GalleryView::openProject, this, &ProjectGalleryWindow::open_project);
  connect(gallery_view_, &GalleryView::createNewProject, this, &ProjectGalleryWindow::create_new_project);
  connect(gallery_view_, &GalleryView::openBlankCanvasRequested, this, &ProjectGalleryWindow::open_blank_canvas);
  connect(gallery_view_, &GalleryView::importProjectRequested, this, &ProjectGalleryWindow::import_project);
}

void ProjectGalleryWindow::refresh() {
  if (gallery_view_) {
    gallery_view_->refresh_projects();
  }
}

void ProjectGalleryWindow::open_project(const std::filesystem::path &path) {
  if (!main_window_) {
    main_window_ = new MainWindow();
  }
  main_window_->open_file(path);
  main_window_->show();
  main_window_->raise();
  main_window_->activateWindow();
  hide();
}

void ProjectGalleryWindow::create_new_project(int width, int height, const QString &title) {
  if (!main_window_) {
    main_window_ = new MainWindow();
  }
  main_window_->create_new_document(width, height, title);
  main_window_->show();
  main_window_->raise();
  main_window_->activateWindow();
  hide();
}

void ProjectGalleryWindow::open_blank_canvas() {
  if (!main_window_) {
    main_window_ = new MainWindow();
  }
  main_window_->show();
  main_window_->raise();
  main_window_->activateWindow();
  hide();
}

void ProjectGalleryWindow::import_project() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Import Project"), QString(), tr("Krita or Image (*.kra *.png)"));
  if (filename.isEmpty()) return;

  std::filesystem::path src_path(filename.toStdString());
  std::filesystem::path dest_dir = GalleryView::projects_directory();
  std::filesystem::path dest_path = dest_dir / src_path.filename();

  std::error_code ec;
  std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing, ec);
  if (!ec) {
    refresh();
  } else {
    QMessageBox::warning(this, tr("Import Error"), tr("Failed to copy project into projects folder."));
  }
}

void ProjectGalleryWindow::closeEvent(QCloseEvent *event) {
  if (main_window_ && !main_window_->isVisible()) {
    qApp->quit();
  }
  QMainWindow::closeEvent(event);
}

} // namespace kreart::ui
