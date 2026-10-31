#pragma once

#include <QDialog>
#include <vector>

class QTableWidget;
class QLineEdit;
class QKeySequenceEdit;

namespace kreart::ui {

class ShortcutDialog : public QDialog {
  Q_OBJECT

public:
  explicit ShortcutDialog(QWidget *parent = nullptr);
  ~ShortcutDialog() override = default;

private slots:
  void filter_shortcuts(const QString &query);
  void reset_to_defaults();
  void export_profile();
  void import_profile();

private:
  void build_ui();
  void populate_table();

  QLineEdit *search_edit_ = nullptr;
  QTableWidget *table_ = nullptr;
  struct RowEntry {
    QString id;
    QKeySequenceEdit *edit = nullptr;
  };
  std::vector<RowEntry> rows_;
};

} // namespace kreart::ui
