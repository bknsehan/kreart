#include <cstdio>

#include "kreart/ui/color_history.h"

namespace {

int failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool same(kreart::core::Rgba8 left, kreart::core::Rgba8 right) {
  return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

} // namespace

int main() {
  using kreart::core::Rgba8;
  using kreart::ui::ColorHistory;

  ColorHistory history;
  check(history.colors().empty(), "history starts empty");

  const Rgba8 red{255, 0, 0, 255};
  const Rgba8 green{0, 255, 0, 255};
  const Rgba8 translucent_red{255, 0, 0, 128};

  history.record(red);
  history.record(green);
  check(history.colors().size() == 2, "records colors");
  check(same(history.colors()[0], green) && same(history.colors()[1], red),
        "newest color is first");

  history.record(red);
  check(history.colors().size() == 2 && same(history.colors()[0], red) &&
            same(history.colors()[1], green),
        "duplicate moves existing color to the front");

  history.record(translucent_red);
  check(history.colors().size() == 3 && same(history.colors()[0], translucent_red),
        "alpha distinguishes colors");

  for (std::size_t i = 0; i < ColorHistory::kMaxColors + 3; ++i) {
    history.record(Rgba8{static_cast<unsigned char>(i), 1, 2, 255});
  }
  check(history.colors().size() == ColorHistory::kMaxColors, "history has a fixed maximum size");
  check(history.colors()[0].r == ColorHistory::kMaxColors + 2,
        "newest color survives trimming");

  history.clear();
  check(history.colors().empty(), "clear removes all colors");
  check(ColorHistory::palette_profiles().size() == 6, "has 6 curated palette profiles");
  check(ColorHistory::default_palette().size() == 8, "default palette has 8 colors");
  check(history.active_profile_index() == 0, "starts on default profile 0");
  check(history.active_palette().size() == 8, "active palette has 8 colors");

  history.set_active_profile_index(1);
  check(history.active_profile_index() == 1, "profile switched to 1");
  check(same(history.active_palette()[0], Rgba8{250, 225, 205, 255}), "profile 1 has Fair Peach");
  return failures == 0 ? 0 : 1;
}
