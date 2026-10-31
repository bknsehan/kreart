#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>

#include "kreart/core/document.h"
#include "kreart/ui/canvas.h"

namespace kreart::ui {

class CanvasSizeDialog : public QDialog {
  Q_OBJECT

public:
  CanvasSizeDialog(QWidget *parent, core::Document &document, Canvas *canvas);

private:
  core::Document &document_;
  Canvas *canvas_ = nullptr;
  QSpinBox *width_spin_ = nullptr;
  QSpinBox *height_spin_ = nullptr;
  QComboBox *anchor_combo_ = nullptr;
};

class ScaleImageDialog : public QDialog {
  Q_OBJECT

public:
  ScaleImageDialog(QWidget *parent, core::Document &document, Canvas *canvas);

private:
  core::Document &document_;
  Canvas *canvas_ = nullptr;
  QSpinBox *width_spin_ = nullptr;
  QSpinBox *height_spin_ = nullptr;
  QCheckBox *constrain_check_ = nullptr;
  QComboBox *filter_combo_ = nullptr;
  bool guard_ = false;
};

} // namespace kreart::ui
