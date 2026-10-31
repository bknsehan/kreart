#include "kreart/brush/sensor_curve.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace kreart::brush {

SensorCurve::SensorCurve(std::string_view curve_spec) {
  parse(curve_spec);
}

bool SensorCurve::parse(std::string_view curve_spec) {
  points_.clear();
  active_ = false;

  if (curve_spec.empty()) {
    return false;
  }

  // If wrapped in <curve>...</curve>, extract inner text
  const auto open_tag = curve_spec.find("<curve>");
  if (open_tag != std::string_view::npos) {
    const auto close_tag = curve_spec.find("</curve>", open_tag + 7);
    if (close_tag != std::string_view::npos) {
      curve_spec = curve_spec.substr(open_tag + 7, close_tag - (open_tag + 7));
    }
  }

  std::size_t start = 0;
  while (start < curve_spec.size()) {
    const auto semi = curve_spec.find(';', start);
    const std::string_view item =
        (semi == std::string_view::npos) ? curve_spec.substr(start) : curve_spec.substr(start, semi - start);
    start = (semi == std::string_view::npos) ? curve_spec.size() : semi + 1;

    const auto comma = item.find(',');
    if (comma == std::string_view::npos) {
      continue;
    }

    try {
      const double x = std::stod(std::string(item.substr(0, comma)));
      const double y = std::stod(std::string(item.substr(comma + 1)));
      points_.push_back(CurvePoint{std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)});
    } catch (...) {
      continue;
    }
  }

  if (points_.size() < 2) {
    points_.clear();
    return false;
  }

  std::sort(points_.begin(), points_.end(),
            [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });

  active_ = true;
  return true;
}

double SensorCurve::evaluate(double input) const {
  if (!active_ || points_.empty()) {
    return std::clamp(input, 0.0, 1.0);
  }

  const double clamped = std::clamp(input, 0.0, 1.0);

  if (clamped <= points_.front().x) {
    return points_.front().y;
  }
  if (clamped >= points_.back().x) {
    return points_.back().y;
  }

  for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
    if (clamped >= points_[i].x && clamped <= points_[i + 1].x) {
      const double dx = points_[i + 1].x - points_[i].x;
      if (dx <= 1e-9) {
        return points_[i].y;
      }
      const double t = (clamped - points_[i].x) / dx;
      return points_[i].y + t * (points_[i + 1].y - points_[i].y);
    }
  }

  return points_.back().y;
}
double SensorCurve::evaluate_context(const StrokeInputContext &ctx) const {
  switch (sensor_type_) {
    case SensorType::PRESSURE:
      return evaluate(ctx.pressure);
    case SensorType::SPEED:
      return evaluate(ctx.speed);
    case SensorType::TILT:
      return evaluate(ctx.tilt);
    case SensorType::DISTANCE:
      return evaluate(ctx.distance);
    case SensorType::FUZZY:
      return evaluate(ctx.fuzzy);
  }
  return evaluate(ctx.pressure);
}

} // namespace kreart::brush
