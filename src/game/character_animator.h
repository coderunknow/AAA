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

 private:
  std::array<PartPose, kBodyPartCount> parts_{};
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
