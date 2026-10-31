#include <QCoreApplication>
#include "kreart/ui/shortcut_dialog.h"
#include "kreart/ui/shortcuts.h"
#include "kreart/ui/app_settings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

namespace kreart::ui {

ShortcutDialog::ShortcutDialog(QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Keyboard Shortcuts"));
  resize(600, 520);
  build_ui();
}

void ShortcutDialog::build_ui() {
  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(16, 16, 16, 16);
  root_layout->setSpacing(12);

  // Top Search Filter
  auto *top_row = new QHBoxLayout();
  top_row->addWidget(new QLabel(tr("Search:"), this));
  search_edit_ = new QLineEdit(this);
  search_edit_->setPlaceholderText(tr("Filter actions or shortcuts..."));
  search_edit_->setClearButtonEnabled(true);
  connect(search_edit_, &QLineEdit::textChanged, this, &ShortcutDialog::filter_shortcuts);
  top_row->addWidget(search_edit_, 1);
  root_layout->addLayout(top_row);

  // Shortcuts Table
  table_ = new QTableWidget(this);
  table_->setColumnCount(2);
  table_->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut")});
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionMode(QAbstractItemView::NoSelection);
  table_->setAlternatingRowColors(true);
  root_layout->addWidget(table_, 1);

  populate_table();

  // Bottom Buttons: Reset to Defaults, Import, Export, Close
  auto *btn_row = new QHBoxLayout();

  auto *reset_btn = new QPushButton(tr("Reset to Defaults"), this);
  connect(reset_btn, &QPushButton::clicked, this, &ShortcutDialog::reset_to_defaults);
  btn_row->addWidget(reset_btn);

  btn_row->addStretch(1);

  auto *import_btn = new QPushButton(tr("Import Profile..."), this);
  connect(import_btn, &QPushButton::clicked, this, &ShortcutDialog::import_profile);
  btn_row->addWidget(import_btn);

  auto *export_btn = new QPushButton(tr("Export Profile..."), this);
  connect(export_btn, &QPushButton::clicked, this, &ShortcutDialog::export_profile);
  btn_row->addWidget(export_btn);

  auto *close_btn = new QPushButton(tr("Close"), this);
  close_btn->setDefault(true);
  connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
  btn_row->addWidget(close_btn);

  root_layout->addLayout(btn_row);
}

void ShortcutDialog::populate_table() {
  const auto &defs = default_shortcuts();
  table_->setRowCount(static_cast<int>(defs.size()));
  rows_.clear();
  rows_.reserve(defs.size());

  for (std::size_t i = 0; i < defs.size(); ++i) {
    const auto &def = defs[i];
    const int r = static_cast<int>(i);

    auto *name_item = new QTableWidgetItem(QCoreApplication::translate("Shortcut", def.label));
    name_item->setFlags(Qt::ItemIsEnabled);
    name_item->setData(Qt::UserRole, QString::fromUtf8(def.id));
    table_->setItem(r, 0, name_item);

    const QKeySequence current = AppSettings::instance().shortcut(QString::fromUtf8(def.id), def.default_seq);
    auto *edit = new QKeySequenceEdit(current, table_);
    edit->setClearButtonEnabled(true);
    const QString action_id = QString::fromUtf8(def.id);
    connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [action_id](const QKeySequence &seq) {
      AppSettings::instance().set_shortcut(action_id, seq);
    });

    table_->setCellWidget(r, 1, edit);
    rows_.push_back(RowEntry{action_id, edit});
  }
}

void ShortcutDialog::filter_shortcuts(const QString &query) {
  const QString q = query.trimmed().toLower();
  for (int r = 0; r < table_->rowCount(); ++r) {
    auto *name_item = table_->item(r, 0);
    auto *edit = qobject_cast<QKeySequenceEdit *>(table_->cellWidget(r, 1));
    const QString name = name_item ? name_item->text().toLower() : QString();
    const QString seq = edit ? edit->keySequence().toString().toLower() : QString();
    const bool match = q.isEmpty() || name.contains(q) || seq.contains(q);
    table_->setRowHidden(r, !match);
  }
}

void ShortcutDialog::reset_to_defaults() {
  AppSettings::instance().clear_shortcuts();
  const auto &defs = default_shortcuts();
  for (std::size_t i = 0; i < defs.size() && i < rows_.size(); ++i) {
    rows_[i].edit->blockSignals(true);
    rows_[i].edit->setKeySequence(defs[i].default_seq);
    rows_[i].edit->blockSignals(false);
  }
}

void ShortcutDialog::export_profile() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export Shortcut Profile"), QString(), tr("Kreart Shortcut Profile (*.ksf)"));
  if (path.isEmpty()) return;

  if (!AppSettings::instance().export_shortcuts(path)) {
    QMessageBox::warning(this, tr("Export Failed"), tr("Failed to export shortcuts to %1").arg(path));
  }
}

void ShortcutDialog::import_profile() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Import Shortcut Profile"), QString(), tr("Kreart Shortcut Profile (*.ksf)"));
  if (path.isEmpty()) return;

  if (AppSettings::instance().import_shortcuts(path)) {
    // Refresh rows
    const auto &defs = default_shortcuts();
    for (std::size_t i = 0; i < defs.size() && i < rows_.size(); ++i) {
      const QKeySequence current = AppSettings::instance().shortcut(QString::fromUtf8(defs[i].id), defs[i].default_seq);
      rows_[i].edit->blockSignals(true);
      rows_[i].edit->setKeySequence(current);
      rows_[i].edit->blockSignals(false);
    }
  } else {
    QMessageBox::warning(this, tr("Import Failed"), tr("Failed to import shortcuts from %1").arg(path));
  }
}

} // namespace kreart::ui
