#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kreart::brush {

struct CurvePoint {
  double x = 0.0;
  double y = 0.0;
};
enum class SensorType {
  PRESSURE,
  SPEED,
  TILT,
  DISTANCE,
  FUZZY,
};

struct StrokeInputContext {
  double pressure = 1.0;
  double speed = 0.0;
  double tilt = 0.0;
  double distance = 0.0;
  double fuzzy = 0.5;
};

class SensorCurve {
public:
  SensorCurve() = default;
  explicit SensorCurve(std::string_view curve_spec);

  bool parse(std::string_view curve_spec);
  double evaluate(double input) const;
  double evaluate_context(const StrokeInputContext &ctx) const;
  SensorType sensor_type() const {
    return sensor_type_;
  }
  void set_sensor_type(SensorType type) {
    sensor_type_ = type;
  }

  bool active() const {
    return active_;
  }
  void set_active(bool active) {
    active_ = active;
  }

  const std::vector<CurvePoint> &points() const {
    return points_;
  }

private:
  bool active_ = false;
  std::vector<CurvePoint> points_;
  SensorType sensor_type_ = SensorType::PRESSURE;
};

} // namespace kreart::brush
