#pragma once
// Third-person orbit camera with spring-arm collision against terrain and props.
#include "core/math.h"

namespace aaa {

class World;
class PlayerController;
struct InputFrame;

struct CameraTuning {
  float distance = 4.3f;
  float minDistance = 1.6f;
  float maxDistance = 7.5f;
  float pivotHeight = 1.55f;
  float shoulderOffset = 0.38f;       // over-the-shoulder framing (metres to the right)
  float mouseSensitivity = 0.0022f;   // radians per pixel
  float stickSensitivity = 2.6f;      // radians per second at full deflection
  float pitchMin = radians(-62.0f);
  float pitchMax = radians(55.0f);
  float baseFov = radians(52.0f);
  float sprintFov = radians(58.0f);
  float pivotHalfLife = 0.06f;
  float collisionRadius = 0.25f;
};

struct CameraView {
  Vec3 eye;
  Vec3 target;
  float fovY;
};

class ThirdPersonCamera {
 public:
  void reset(const PlayerController& player);
  void update(float dt, const InputFrame& input, const PlayerController& player, const World& world);
  // Benchmark hook (PROMPT §11): drive the framing deterministically, ignoring
  // player look input. Cleared with clearBenchView().
  void setBenchView(float yaw, float pitch, float distance) {
    benchActive_ = true;
    benchYaw_ = yaw;
    benchPitch_ = pitch;
    benchDistance_ = distance;
  }
  void clearBenchView() { benchActive_ = false; }
  bool benchActive() const { return benchActive_; }

  const CameraView& view() const { return view_; }
  // Render-interpolated view between the previous and current simulation state
  // (PROMPT §8.6: stable camera smoothing independent of the render frame rate).
  CameraView view(float alpha) const {
    CameraView v;
    v.eye = lerp(prevView_.eye, view_.eye, alpha);
    v.target = lerp(prevView_.target, view_.target, alpha);
    v.fovY = lerp(prevView_.fovY, view_.fovY, alpha);
    return v;
  }
  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }
  CameraTuning& tuning() { return tuning_; }
  // Forward/right on the ground plane, for camera-relative movement.
  Vec2 groundForward() const { return {std::sin(yaw_), std::cos(yaw_)}; }
  Vec2 groundRight() const { return {std::cos(yaw_), -std::sin(yaw_)}; }

 private:
  float obstructionDistance(Vec3 pivot, Vec3 dir, float maxDist, const World& world) const;

  CameraTuning tuning_;
  CameraView view_{};
  CameraView prevView_{};  // view at the previous simulation step (interpolation source)
  float yaw_ = 0.0f;
  float pitch_ = radians(-12.0f);  // negative: looking slightly down
  float desiredDistance_ = 4.3f;
  float currentDistance_ = 4.3f;
  float fov_ = radians(52.0f);
  Vec3 pivot_;
  float idleTime_ = 0.0f;
  bool benchActive_ = false;
  float benchYaw_ = 0.0f, benchPitch_ = 0.0f, benchDistance_ = 0.0f;
};

}  // namespace aaa
