#pragma once
// Procedural locomotion for the stand-in survivor. Produces world-space transforms
// for a small set of rigid body parts. Renderer-independent.
#include <array>
#include <cstdint>

#include "core/math.h"

namespace aaa {

class PlayerController;

enum class BodyPart : uint8_t {
  Pelvis, Torso, Head, Hat,
  UpperArmL, ForearmL, HandL, UpperArmR, ForearmR, HandR,
  ThighL, ShinL, FootL, ThighR, ShinR, FootR,
  Backpack, Bedroll,
  Count
};
constexpr int kBodyPartCount = static_cast<int>(BodyPart::Count);

enum class PartShape : uint8_t { Capsule, Box, Sphere, Cone };
enum class PartMaterial : uint8_t { Skin, Jacket, Trousers, Leather, Canvas, Straw, Fur, FurDark, Eyes };

struct PartPose {
  Mat4 transform;  // unit primitive -> world (includes scale)
  PartShape shape;
  PartMaterial material;
};

class CharacterAnimator {
 public:
  void update(float dt, const PlayerController& player);
  const std::array<PartPose, kBodyPartCount>& parts() const { return parts_; }
  float footstepPhase() const { return phase_; }
  // Returns true once per footfall (for audio / dust VFX).
  bool consumeFootstep() { const bool f = footstep_; footstep_ = false; return f; }
  // Character root (world placement) of the current and previous simulation step,
  // and the render-interpolated root between them (PROMPT §8.6 render interpolation).
  const Mat4& root() const { return root_; }
  Mat4 renderRoot(float alpha) const {
    const Vec3 p = lerp(prevPos_, pos_, alpha);
    const float yaw = prevYaw_ + wrapAngle(yaw_ - prevYaw_) * alpha;  // wrap-safe
    return Mat4::translation(p) * Mat4::rotationY(yaw);
  }

 private:
  std::array<PartPose, kBodyPartCount> parts_{};
  Mat4 root_{};
  Vec3 pos_{}, prevPos_{};
  float yaw_ = 0.0f, prevYaw_ = 0.0f;
  float phase_ = 0.0f;
  float lastDistance_ = 0.0f;
  float moveWeight_ = 0.0f;   // 0 idle .. 1 moving
  float runWeight_ = 0.0f;    // 0 walk .. 1 sprint
  float crouchWeight_ = 0.0f;
  float airWeight_ = 0.0f;
  float leanForward_ = 0.0f;
  float leanSide_ = 0.0f;
  float time_ = 0.0f;
  float prevSpeed_ = 0.0f;
  bool footstep_ = false;
  bool initialised_ = false;
};

}  // namespace aaa
