#include "game/time_of_day.h"

#include <cmath>

namespace aaa {

Vec3 TimeOfDay::sunDirection() const {
  // Sun rises in the east (+X), culminates at ~58 degrees toward the south (-Z).
  const float t = (hours_ - 6.0f) / 12.0f * kPi;  // 0 at 06:00, pi at 18:00
  const float elevation = std::sin(t) * radians(58.0f);
  const float azimuth = t;                          // east -> south -> west
  const Vec3 d{std::cos(azimuth) * std::cos(elevation), std::sin(elevation), -std::sin(azimuth) * std::cos(elevation) * 0.8f};
  return normalize(d);
}

float TimeOfDay::daylight() const { return smoothstep(-0.08f, 0.18f, sunDirection().y); }

}  // namespace aaa
