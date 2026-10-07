#pragma once
// Kinematic third-person character controller on the heightfield with circle
// colliders for trunks, rocks and spires.
#include "core/math.h"

namespace aaa {

class World;

struct PlayerTuning {
  float walkSpeed = 1.6f;
  float runSpeed = 3.7f;     // default jog
  float sprintSpeed = 6.2f;
  float crouchSpeed = 1.15f;
  float acceleration = 11.0f;  // m/s^2 toward target speed
  float deceleration = 14.0f;
  float airControl = 0.25f;
  float turnHalfLife = 0.07f;  // facing smoothing
  float jumpSpeed = 4.4f;
  float gravity = 18.0f;       // slightly above 9.81 for less floaty jumps
  float radius = 0.32f;
  float height = 1.78f;
  float maxWalkableSlope = 0.38f;  // 1 - cos(~52 deg)
  float staminaMax = 100.0f;
  float sprintDrain = 14.0f;       // per second
  float staminaRegen = 9.0f;
};

enum class Locomotion { Idle, Walk, Run, Sprint, Crouch, Airborne, Wading };

class PlayerController {
 public:
  void spawn(Vec3 position, float yaw);
  // `wish` is the desired move direction in world XZ (magnitude 0..1).
  void update(float dt, Vec2 wish, bool sprint, bool walk, bool jump, bool crouchToggle, const World& world);

  Vec3 position() const { return pos_; }
  Vec3 velocity() const { return vel_; }
  float facingYaw() const { return yaw_; }
  float horizontalSpeed() const { return length(Vec2{vel_.x, vel_.z}); }
  bool grounded() const { return grounded_; }
  bool crouching() const { return crouching_; }
  float stamina() const { return stamina_; }
  float waterDepth() const { return waterDepth_; }
  float turnRate() const { return turnRate_; }
  float landingImpact() const { return landingImpact_; }  // decays after a landing
  Locomotion locomotion() const { return loco_; }
  float distanceTravelled() const { return distance_; }
  const PlayerTuning& tuning() const { return tuning_; }

 private:
  void resolveCollisions(const World& world);

  PlayerTuning tuning_;
  Vec3 pos_;
  Vec3 vel_;
  float yaw_ = 0.0f;
  float turnRate_ = 0.0f;
  bool grounded_ = true;
  bool crouching_ = false;
  bool sprintLocked_ = false;  // exhausted: must release sprint and recover
  float stamina_ = 100.0f;
  float waterDepth_ = 0.0f;
  float landingImpact_ = 0.0f;
  float distance_ = 0.0f;
  Locomotion loco_ = Locomotion::Idle;
};

}  // namespace aaa
