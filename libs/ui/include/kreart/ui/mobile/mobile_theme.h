#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace kreart::ui::mobile {

// "Ink & Amber" identity for the touch-first Android studio. These values are
// the single source of truth for mobile chrome; no desktop stylesheet is
// reused. Lengths are logical pixels (dp == px at 1x logical density).
struct Tokens {
  QColor workspace_bg{14, 15, 18};      // #0E0F12
  QColor surface{22, 24, 29};           // #16181D
  QColor surface_raised{30, 33, 39};    // #1E2127
  QColor hairline{255, 255, 255, 26};   // 10% white
  QColor text{236, 238, 243};
  QColor text_muted{154, 160, 173};
  QColor accent{232, 180, 74};          // #E8B44A Ink & Amber
  QColor accent_pressed{198, 148, 52};
  QColor accent_ink{26, 20, 8};         // text on amber
  QColor danger{244, 92, 92};
  int radius_sheet = 24;
  int radius_card = 16;
  int radius_control = 12;
  int touch = 48;                       // minimum tap target
  int gap = 12;
};

const Tokens &tokens();

// Overrides the accent triple at runtime (Settings > accent swatches). Callers
// are responsible for re-applying sheet_qss() afterwards.
void set_accent_colors(const QColor &accent, const QColor &pressed, const QColor &ink);
void reset_accent_colors();

// Font ramp: title 20sp semibold, section 15sp semibold, body 14sp, caption 12sp.
QFont font_title();
QFont font_section();
QFont font_body();
QFont font_caption();

// QSS for QWidget#MobileSheet and its children. Applied application-wide only
// from AndroidAppWindow's constructor; never used by the desktop UI.
QString sheet_qss();

// Colour helpers shared by the mobile chrome.
QString css_color(const QColor &color, bool with_alpha = false);

} // namespace kreart::ui::mobile
