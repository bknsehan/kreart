#pragma once

#include <algorithm>
#include <string>
#include <utility>

#include "kreart/core/tiled_image.h"

namespace kreart::core {

enum class ScreentonePattern {
  DOT_60,
  DOT_50,
  DOT_40,
  DOT_80,
  LINE_45,
  CROSSHATCH,
};

class Layer {
public:
  std::size_t id() const {
    return id_;
  }
  void set_id(std::size_t id) {
    id_ = id;
  }

  Layer(std::string name, int width, int height, bool fill_white = false)
      : name_(std::move(name)), image_(width, height) {
    if (fill_white) {
      image_.fill(Rgba8{255, 255, 255, 255});
    }
  }

  const std::string &name() const {
    return name_;
  }
  void set_name(std::string name) {
    name_ = std::move(name);
  }

  bool visible() const {
    return visible_;
  }
  void set_visible(bool visible) {
    visible_ = visible;
  }

  double opacity() const {
    return opacity_;
  }
  void set_opacity(double opacity) {
    opacity_ = std::clamp(opacity, 0.0, 1.0);
  }

  const std::string &composite_op() const {
    return composite_op_;
  }
  void set_composite_op(std::string op) {
    composite_op_ = std::move(op);
  }

  bool locked() const {
    return locked_;
  }
  void set_locked(bool locked) {
    locked_ = locked;
  }
  bool alpha_locked() const {
    return alpha_locked_;
  }
  void set_alpha_locked(bool locked) {
    alpha_locked_ = locked;
    image_.set_alpha_locked(locked);
  }

  bool clipping_mask() const {
    return clipping_mask_;
  }
  void set_clipping_mask(bool clipping) {
    clipping_mask_ = clipping;
  }

  bool screentone() const {
    return screentone_;
  }
  void set_screentone(bool st) {
    screentone_ = st;
  }
  double screentone_frequency() const {
    return screentone_frequency_;
  }
  void set_screentone_frequency(double freq) {
    screentone_frequency_ = std::clamp(freq, 10.0, 200.0);
  }
  double screentone_angle() const {
    return screentone_angle_;
  }
  void set_screentone_angle(double angle) {
    screentone_angle_ = angle;
  }
  ScreentonePattern screentone_pattern() const {
    return screentone_pattern_;
  }
  void set_screentone_pattern(ScreentonePattern pat) {
    screentone_pattern_ = pat;
  }


  bool is_group() const {
    return is_group_;
  }
  void set_is_group(bool grp) {
    is_group_ = grp;
  }
  bool expanded() const {
    return expanded_;
  }
  void set_expanded(bool exp) {
    expanded_ = exp;
  }
  std::size_t parent_id() const {
    return parent_id_;
  }
  void set_parent_id(std::size_t pid) {
    parent_id_ = pid;
  }


  TiledImage &image() {
    return image_;
  }
  const TiledImage &image() const {
    return image_;
  }

private:
  std::string name_;
  bool visible_ = true;
  double opacity_ = 1.0;
  std::string composite_op_ = "normal";
  bool locked_ = false;
  bool alpha_locked_ = false;
  bool clipping_mask_ = false;
  bool screentone_ = false;
  double screentone_frequency_ = 60.0;
  double screentone_angle_ = 45.0;
  ScreentonePattern screentone_pattern_ = ScreentonePattern::DOT_60;
  bool is_group_ = false;
  bool expanded_ = true;
  std::size_t parent_id_ = 0;
  std::size_t id_ = 0;
  TiledImage image_;
};

struct LayerSnapshot {
  std::string name;
  bool visible = true;
  double opacity = 1.0;
  std::string composite_op = "normal";
  bool locked = false;
  bool alpha_locked = false;
  bool clipping_mask = false;
  bool screentone = false;
  double screentone_frequency = 60.0;
  double screentone_angle = 45.0;
  ScreentonePattern screentone_pattern = ScreentonePattern::DOT_60;
  bool is_group = false;
  bool expanded = true;
  std::size_t id = 0;
  std::size_t parent_id = 0;
  ImageSnapshot image;
};
} // namespace kreart::core
