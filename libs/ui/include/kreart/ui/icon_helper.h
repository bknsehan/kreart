#pragma once

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QString>

namespace kreart::ui {

class IconHelper {
public:
  static QPixmap render_symbolic(const QString &resource_path, const QColor &color, int size = 24);
  static QIcon load_symbolic(const QString &resource_path, const QColor &color = QColor(240, 242, 248), int size = 24);
  static QIcon load_symbolic(const QString &resource_path, const QPalette &palette, int size = 24);
};

} // namespace kreart::ui
