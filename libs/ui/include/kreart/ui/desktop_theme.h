#pragma once

namespace kreart::ui {

// Applies the user's theme/accent preference to the whole desktop application:
// a QPalette plus a small desktop-only stylesheet. Mobile applies its own sheet
// (mobile::sheet_qss()) and must not call this.
void apply_desktop_theme();

} // namespace kreart::ui
