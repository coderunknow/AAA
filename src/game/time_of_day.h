#pragma once
#include "core/math.h"

namespace aaa {

// Day/night cycle. One in-game day lasts `dayLengthSeconds` of real time.
class TimeOfDay {
 public:
  void update(float dt) { hours_ = std::fmod(hours_ + dt * 24.0f / dayLengthSeconds, 24.0f); }
  float hours() const { return hours_; }
  void setHours(float h) { hours_ = std::fmod(std::fmax(h, 0.0f), 24.0f); }
  float dayLengthSeconds = 30.0f * 60.0f;
  bool paused = false;

  // Unit vector pointing from the ground toward the sun.
  Vec3 sunDirection() const;
  // 0 at night, 1 in full day (smooth around sunrise/sunset).
  float daylight() const;

 private:
  float hours_ = 7.4f;  // misty early morning
};

}  // namespace aaa
