#pragma once

#include <filesystem>
#include <vector>

#include <QDateTime>
#include <QHash>
#include <QPixmap>
#include <QWidget>

class QGridLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QPushButton;

namespace kreart::ui::mobile {

class EditorSheets;
class MobileHud;
class MobileSheet;
struct EditorContext;

struct MobileProjectItem {
  std::filesystem::path file_path;
  std::filesystem::path thumbnail_path;
  QPixmap thumbnail_pixmap;
  QString title;
  int width = 1920;
  int height = 1080;
  QDateTime last_modified;
};

class MobileGalleryCard;

// Touch-first project gallery: fluid 2/3-column grid with large tap targets,
// bottom sheets for card actions and canvas creation.
class MobileGallery : public QWidget {
  Q_OBJECT

public:
  explicit MobileGallery(QWidget *parent = nullptr);
  ~MobileGallery() override;

  void refresh_projects();

  int column_count() const { return columns_; }
  int card_width() const;
  // Card widget at `index` (top-left to bottom-right). Used by tests and by the
  // grid re-layout pass; null when out of range.
  QWidget *card_at(int index) const;
  const std::vector<MobileProjectItem> &projects() const { return projects_; }

signals:
  void openProject(const std::filesystem::path &path);
  void createNewProject(int width, int height, const QString &title);
  void importProjectRequested();
  void openBlankCanvasRequested();

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void build_ui();
  void rebuild_grid();
  void show_card_actions(const MobileProjectItem &item);
  void show_new_canvas_sheet();
  void show_rename_sheet(const MobileProjectItem &item);
  void show_delete_confirm(const MobileProjectItem &item);
  void duplicate_project(const MobileProjectItem &item);
  void export_project_png(const MobileProjectItem &item);
  void show_overview_sheet();
  void hud_message(const QString &text);

  std::vector<MobileProjectItem> projects_;
  QHash<QString, QPixmap> thumbnail_cache_;

  QScrollArea *scroll_area_ = nullptr;
  QWidget *grid_container_ = nullptr;
  QGridLayout *grid_layout_ = nullptr;
  QWidget *empty_container_ = nullptr;
  QLabel *empty_label_ = nullptr;
  int columns_ = 2;

  std::vector<MobileGalleryCard *> cards_;
  QPushButton *overflow_btn_ = nullptr;
  MobileHud *hud_ = nullptr;
  MobileSheet *active_sheet_ = nullptr;
};

} // namespace kreart::ui::mobile
