#include "game/player.h"

#include <cmath>

#include "world/world.h"

namespace aaa {

void PlayerController::spawn(Vec3 position, float yaw) {
  pos_ = position;
  vel_ = {};
  yaw_ = yaw;
  grounded_ = true;
  stamina_ = tuning_.staminaMax;
}

void PlayerController::update(float dt, Vec2 wish, bool sprint, bool walk, bool jump, bool crouchToggle,
                              const World& world) {
  const PlayerTuning& T = tuning_;
  if (crouchToggle) crouching_ = !crouching_;

  float wishLen = length(wish);
  if (wishLen > 1.0f) { wish = wish * (1.0f / wishLen); wishLen = 1.0f; }

  waterDepth_ = world.waterDepth(pos_.x, pos_.z);
  const bool wading = waterDepth_ > 0.25f;

  // Stamina: sprinting drains, everything else regenerates.
  if (!sprint) sprintLocked_ = false;
  const bool canSprint = sprint && !sprintLocked_ && !crouching_ && grounded_ && wishLen > 0.5f;
  if (canSprint) {
    stamina_ -= T.sprintDrain * dt;
    if (stamina_ <= 0.0f) { stamina_ = 0.0f; sprintLocked_ = true; }
  } else {
    stamina_ = std::fmin(T.staminaMax, stamina_ + T.staminaRegen * dt * (horizontalSpeed() < 0.5f ? 1.6f : 1.0f));
  }

  float targetSpeed = crouching_ ? T.crouchSpeed : (canSprint ? T.sprintSpeed : (walk ? T.walkSpeed : T.runSpeed));
  targetSpeed *= wishLen;
  if (wading) targetSpeed *= clampf(1.0f - waterDepth_ * 0.45f, 0.35f, 0.8f);

  // Slope: slower uphill, cannot climb cliffs.
  const Vec3 n = world.heightfield().normal(pos_.x, pos_.z);
  const float slope = 1.0f - n.y;
  Vec2 desired = wishLen > 1e-4f ? wish * (targetSpeed / wishLen) : Vec2{};
  if (wishLen > 1e-4f) {
    const Vec2 downhill = Vec2{n.x, n.z};
    const float dl = length(downhill);
    if (dl > 1e-4f) {
      const Vec2 dh = downhill * (1.0f / dl);
      const float uphill = -dot(desired * (1.0f / std::fmax(targetSpeed, 1e-4f)), dh);  // >0 going uphill
      if (uphill > 0.0f) {
        desired = desired * (1.0f - 0.9f * smoothstep(0.12f, T.maxWalkableSlope, slope) * uphill);
        if (slope > T.maxWalkableSlope) {
          // Remove the uphill component entirely on unwalkable slopes.
          const float into = dot(desired, dh);
          if (into < 0.0f) desired = desired - dh * into;
        }
      }
    }
  }

  // Accelerate horizontal velocity toward the desired velocity.
  Vec2 hv{vel_.x, vel_.z};
  const Vec2 delta = desired - hv;
  const float dlen = length(delta);
  const bool speeding = dot(desired, desired) >= dot(hv, hv);
  float rate = (speeding ? T.acceleration : T.deceleration) * (grounded_ ? 1.0f : T.airControl);
  if (wading) rate *= 0.6f;
  const float stepLen = rate * dt;
  hv = dlen <= stepLen ? desired : hv + delta * (stepLen / dlen);
  vel_.x = hv.x;
  vel_.z = hv.y;

  // Facing follows movement direction smoothly.
  const float prevYaw = yaw_;
  if (length(hv) > 0.15f && wishLen > 0.05f) {
    const float targetYaw = std::atan2(desired.x, desired.y);
    yaw_ = wrapAngle(yaw_ + wrapAngle(targetYaw - yaw_) * dampFactor(T.turnHalfLife * (canSprint ? 1.6f : 1.0f), dt));
  }
  turnRate_ = lerp(turnRate_, wrapAngle(yaw_ - prevYaw) / std::fmax(dt, 1e-4f), dampFactor(0.08f, dt));

  // Vertical.
  if (grounded_ && jump && !crouching_ && !wading) {
    vel_.y = T.jumpSpeed;
    grounded_ = false;
  }
  if (!grounded_) vel_.y -= T.gravity * dt;

  const Vec3 before = pos_;
  pos_ += vel_ * dt;
  resolveCollisions(world);

  // World bounds (the ring of peaks is the visual boundary; this is the safety net).
  const float lim = world.heightfield().halfSize() - 24.0f;
  pos_.x = clampf(pos_.x, -lim, lim);
  pos_.z = clampf(pos_.z, -lim, lim);

  const float ground = world.groundHeight(pos_.x, pos_.z);
  landingImpact_ = std::fmax(0.0f, landingImpact_ - dt * 2.5f);
  if (grounded_) {
    // Stick to the ground when walking downhill; leave it on cliffs edges.
    if (pos_.y - ground < 0.45f + horizontalSpeed() * dt * 2.0f) pos_.y = ground;
    else grounded_ = false;
    vel_.y = 0.0f;
  } else if (pos_.y <= ground) {
    landingImpact_ = clampf(-vel_.y / 9.0f, 0.0f, 1.0f);
    landingSpeed_ = -vel_.y;
    pos_.y = ground;
    vel_.y = 0.0f;
    grounded_ = true;
  }

  distance_ += length(Vec2{pos_.x - before.x, pos_.z - before.z});
  const float sp = horizontalSpeed();
  if (!grounded_) loco_ = Locomotion::Airborne;
  else if (wading && sp > 0.2f) loco_ = Locomotion::Wading;
  else if (crouching_) loco_ = Locomotion::Crouch;
  else if (sp < 0.15f) loco_ = Locomotion::Idle;
  else if (sp < 2.4f) loco_ = Locomotion::Walk;
  else if (sp < 4.9f) loco_ = Locomotion::Run;
  else loco_ = Locomotion::Sprint;
}

void PlayerController::resolveCollisions(const World& world) {
  for (int iter = 0; iter < 3; ++iter) {
    bool any = false;
    world.forEachColliderNear(pos_.xz(), tuning_.radius, [&](const CircleCollider& c) {
      if (pos_.y > c.top) return;  // standing above it
      Vec2 d = pos_.xz() - c.center;
      const float minDist = c.radius + tuning_.radius;
      const float dist = length(d);
      if (dist >= minDist) return;
      if (dist < 1e-4f) d = {1.0f, 0.0f};
      else d = d * (1.0f / dist);
      const float push = minDist - dist;
      pos_.x += d.x * push;
      pos_.z += d.y * push;
      // Remove the velocity component into the obstacle (slide along it).
      const float into = vel_.x * d.x + vel_.z * d.y;
      if (into < 0.0f) { vel_.x -= d.x * into; vel_.z -= d.y * into; }
      any = true;
    });
    if (!any) break;
  }
}

}  // namespace aaa
