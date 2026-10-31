#include "kreart/ui/icon_helper.h"
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSvgRenderer>
#include <string>
#include <unordered_map>

namespace kreart::ui {
namespace {

// Rasterizing an SVG at five sizes for two states is expensive and the layer
// panel asks for the same icons on every row, so results are memoised.
struct IconCache {
  QMutex mutex;
  std::unordered_map<std::string, QPixmap> pixmaps;
  std::unordered_map<std::string, QIcon> icons;
};

IconCache &icon_cache() {
  static IconCache c;
  return c;
}

std::string icon_key(const QString &path, const QColor &color, int size) {
  return path.toStdString() + '|' + color.name(QColor::HexArgb).toStdString() + '|' +
         std::to_string(size);
}

} // namespace

QPixmap IconHelper::render_symbolic(const QString &resource_path, const QColor &color, int size) {
  const std::string key = icon_key(resource_path, color, size);
  {
    QMutexLocker lock(&icon_cache().mutex);
    const auto found = icon_cache().pixmaps.find(key);
    if (found != icon_cache().pixmaps.end()) {
      return found->second;
    }
  }

  QSvgRenderer renderer;
  bool loaded = false;
  if (!resource_path.isEmpty()) {
    loaded = renderer.load(resource_path);
  }
  if (!loaded) {
    const QString stripped_filename = QFileInfo(resource_path).fileName();
    if (!stripped_filename.isEmpty()) {
      loaded = renderer.load(":/icons/icons/" + stripped_filename);
      if (!loaded) {
        loaded = renderer.load(":/icons/" + stripped_filename);
      }
    }
  }
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);

  if (renderer.isValid()) {
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
  } else {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 2.0));
    painter.drawRect(2, 2, size - 4, size - 4);
  }

  QMutexLocker lock(&icon_cache().mutex);
  icon_cache().pixmaps.emplace(key, pixmap);
  return pixmap;
}

QIcon IconHelper::load_symbolic(const QString &resource_path, const QColor &color, int size) {
  const std::string key = icon_key(resource_path, color, size);
  {
    QMutexLocker lock(&icon_cache().mutex);
    const auto found = icon_cache().icons.find(key);
    if (found != icon_cache().icons.end()) {
      return found->second;
    }
  }

  QIcon icon;
  QColor disabledColor = color;
  disabledColor.setAlpha(90);

  for (int s : {16, 20, 24, 32, 48}) {
    QPixmap pNormal = render_symbolic(resource_path, color, s);
    if (!pNormal.isNull()) {
      icon.addPixmap(pNormal, QIcon::Normal);
    }
    QPixmap pDisabled = render_symbolic(resource_path, disabledColor, s);
    if (!pDisabled.isNull()) {
      icon.addPixmap(pDisabled, QIcon::Disabled);
    }
  }

  QMutexLocker lock(&icon_cache().mutex);
  icon_cache().icons.emplace(key, icon);
  return icon;
}

QIcon IconHelper::load_symbolic(const QString &resource_path, const QPalette &palette, int size) {
  QColor normalColor = palette.color(QPalette::WindowText);
  // Auto-contrast guard: if background is dark, ensure icon color is high contrast
  if (palette.color(QPalette::Window).lightness() < 128) {
    if (normalColor.lightness() < 128) {
      normalColor = QColor(240, 242, 248);
    }
  } else {
    if (normalColor.lightness() >= 128) {
      normalColor = QColor(25, 27, 32);
    }
  }
  return load_symbolic(resource_path, normalColor, size);
}

} // namespace kreart::ui
