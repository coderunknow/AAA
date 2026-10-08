#include "game/camera.h"

#include <cmath>

#include "game/input.h"
#include "game/player.h"
#include "world/world.h"

namespace aaa {

void ThirdPersonCamera::reset(const PlayerController& player) {
  yaw_ = player.facingYaw();
  pivot_ = player.position() + Vec3{0, tuning_.pivotHeight, 0};
  desiredDistance_ = currentDistance_ = tuning_.distance;
  fov_ = tuning_.baseFov;
}

float ThirdPersonCamera::obstructionDistance(Vec3 pivot, Vec3 dir, float maxDist, const World& world) const {
  float best = maxDist;
  // Terrain: march slightly above the surface so the lens never dips into the ground.
  float hit = 0;
  if (world.heightfield().raycast(pivot + Vec3{0, -0.35f, 0}, dir, maxDist, hit)) best = std::fmin(best, hit);
  // Trunks / rocks / spires as vertical cylinders.
  const Vec2 o = pivot.xz();
  const Vec2 d2{dir.x, dir.z};
  const float d2len = length(d2);
  if (d2len > 1e-4f) {
    const Vec2 dn = d2 * (1.0f / d2len);
    world.forEachColliderNear(o + dn * (maxDist * 0.5f), maxDist * 0.5f + 1.0f, [&](const CircleCollider& c) {
      const float r = c.radius + tuning_.collisionRadius;
      const Vec2 oc = o - c.center;
      const float b = dot(oc, dn);
      const float cc = dot(oc, oc) - r * r;
      if (cc < 0.0f) return;  // pivot inside: ignore (player is pushed out anyway)
      const float disc = b * b - cc;
      if (disc < 0.0f) return;
      const float tPlanar = -b - std::sqrt(disc);
      if (tPlanar < 0.0f) return;
      const float t = tPlanar / d2len;  // distance along the 3D ray
      const float y = pivot.y + dir.y * t;
      if (y < c.top && t < best) best = t;
    });
  }
  return best;
}

void ThirdPersonCamera::update(float dt, const InputFrame& input, const PlayerController& player, const World& world) {
  prevView_ = view_;  // interpolation source for render-time smoothing
  const CameraTuning& T = tuning_;
  // Look input.
  yaw_ += input.lookDelta.x * T.mouseSensitivity + input.lookRate.x * T.stickSensitivity * dt;
  pitch_ -= input.lookDelta.y * T.mouseSensitivity + input.lookRate.y * T.stickSensitivity * dt * 0.7f;
  pitch_ = clampf(pitch_, T.pitchMin, T.pitchMax);
  yaw_ = wrapAngle(yaw_);
  desiredDistance_ = clampf(desiredDistance_ - input.zoom * 0.5f, T.minDistance, T.maxDistance);

  // Gentle auto-recentre behind a moving player when the user is not steering the camera.
  const bool userLooking = std::fabs(input.lookDelta.x) + std::fabs(input.lookDelta.y) > 0.01f ||
                           std::fabs(input.lookRate.x) + std::fabs(input.lookRate.y) > 0.05f;
  idleTime_ = userLooking ? 0.0f : idleTime_ + dt;
  if (idleTime_ > 1.5f && player.horizontalSpeed() > 2.0f) {
    const float k = dampFactor(1.6f, dt) * smoothstep(1.5f, 3.0f, idleTime_);
    yaw_ = wrapAngle(yaw_ + wrapAngle(player.facingYaw() - yaw_) * k);
  }

  // Pivot follows the player with light smoothing (vertical a bit softer for steps).
  const float crouchDrop = player.crouching() ? 0.45f : 0.0f;
  const Vec3 targetPivot = player.position() + Vec3{0, T.pivotHeight - crouchDrop, 0};
  const float kXZ = dampFactor(T.pivotHalfLife, dt), kY = dampFactor(T.pivotHalfLife * 2.2f, dt);
  pivot_.x = lerp(pivot_.x, targetPivot.x, kXZ);
  pivot_.z = lerp(pivot_.z, targetPivot.z, kXZ);
  pivot_.y = lerp(pivot_.y, targetPivot.y, kY);

  // Contextual distance: pull back slightly when sprinting, in when crouched.
  float contextual = desiredDistance_;
  if (player.locomotion() == Locomotion::Sprint) contextual += 0.6f;
  if (player.crouching()) contextual -= 0.7f;

  const float cp = std::cos(pitch_), sp = std::sin(pitch_);
  const Vec3 forward{std::sin(yaw_) * cp, sp, std::cos(yaw_) * cp};
  const Vec3 right{std::cos(yaw_), 0.0f, -std::sin(yaw_)};
  const Vec3 shoulderPivot = pivot_ + right * T.shoulderOffset * smoothstep(1.0f, 3.0f, currentDistance_);
  const Vec3 back = -forward;

  const float allowed = std::fmax(0.5f, obstructionDistance(shoulderPivot, back, contextual, world) - 0.2f);
  // Snap in fast when obstructed, ease out slowly when clear.
  const float k = allowed < currentDistance_ ? dampFactor(0.03f, dt) : dampFactor(0.35f, dt);
  currentDistance_ = lerp(currentDistance_, std::fmin(allowed, contextual), k);

  Vec3 eye = shoulderPivot + back * currentDistance_;
  const float minEye = world.groundHeight(eye.x, eye.z) + 0.35f;
  if (eye.y < minEye) eye.y = minEye;

  const float targetFov = player.locomotion() == Locomotion::Sprint ? T.sprintFov : T.baseFov;
  fov_ = lerp(fov_, targetFov, dampFactor(0.4f, dt));

  view_.eye = eye;
  view_.target = shoulderPivot + forward * 10.0f;
  view_.fovY = fov_;
}

}  // namespace aaa
