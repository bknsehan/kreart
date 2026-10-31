#pragma once

#include <filesystem>
#include <vector>

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDateTime>
#include <QPixmap>

namespace kreart::ui {

struct ProjectItem {
  std::filesystem::path file_path;
  std::filesystem::path thumbnail_path;
  QPixmap thumbnail_pixmap;
  QString title;
  int width = 1920;
  int height = 1080;
  QDateTime last_modified;
};

class GalleryCard;

class GalleryView : public QWidget {
  Q_OBJECT

public:
  explicit GalleryView(QWidget *parent = nullptr);
  ~GalleryView() override = default;

  void refresh_projects();
  static std::filesystem::path projects_directory();

signals:
  void openProject(const std::filesystem::path &path);
  void createNewProject(int width, int height, const QString &title);
  void openBlankCanvasRequested();
  void importProjectRequested();
protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void build_ui();
  void rebuild_grid();
  void on_new_canvas_clicked();

  std::vector<ProjectItem> projects_;
  QScrollArea *scroll_area_ = nullptr;
  QWidget *grid_container_ = nullptr;
  QGridLayout *grid_layout_ = nullptr;
  QWidget *empty_container_ = nullptr;
  QLabel *empty_label_ = nullptr;
  int columns_ = 2;
};

} // namespace kreart::ui
