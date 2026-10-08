#include "game/character_animator.h"

#include <cmath>

#include "game/player.h"

namespace aaa {
namespace {

// Unit capsule/box primitives hang from the joint along -Y with length 1.
Mat4 limb(const Mat4& joint, float radius, float len) { return joint * Mat4::scale({radius, len, radius}); }
// Positive swing moves a limb forward (+Z).
Mat4 swingX(float a) { return Mat4::rotationX(-a); }

}  // namespace

void CharacterAnimator::update(float dt, const PlayerController& player) {
  time_ += dt;
  const float speed = player.horizontalSpeed();
  if (!initialised_) { lastDistance_ = player.distanceTravelled(); initialised_ = true; }

  // Blend weights.
  const float moving = smoothstep(0.1f, 1.0f, speed);
  moveWeight_ = lerp(moveWeight_, moving, dampFactor(0.08f, dt));
  runWeight_ = lerp(runWeight_, smoothstep(2.0f, 6.0f, speed), dampFactor(0.15f, dt));
  crouchWeight_ = lerp(crouchWeight_, player.crouching() ? 1.0f : 0.0f, dampFactor(0.1f, dt));
  airWeight_ = lerp(airWeight_, player.grounded() ? 0.0f : 1.0f, dampFactor(player.grounded() ? 0.05f : 0.1f, dt));
  const float accel = (speed - prevSpeed_) / std::fmax(dt, 1e-4f);
  prevSpeed_ = speed;
  leanForward_ = lerp(leanForward_, 0.04f * moveWeight_ + 0.16f * runWeight_ + clampf(accel * 0.02f, -0.12f, 0.12f),
                      dampFactor(0.12f, dt));
  leanSide_ = lerp(leanSide_, clampf(-player.turnRate() * speed * 0.035f, -0.3f, 0.3f), dampFactor(0.1f, dt));

  // Phase advances with distance (no foot sliding): one cycle = two steps.
  const float dist = player.distanceTravelled();
  const float stride = lerp(0.72f, 1.55f, runWeight_) * lerp(1.0f, 0.6f, crouchWeight_);
  if (player.grounded()) {
    const float prevPhase = phase_;
    phase_ += (dist - lastDistance_) / stride * kPi;
    const float a = std::floor(prevPhase / kPi), b = std::floor(phase_ / kPi);
    if (b != a && moveWeight_ > 0.3f) footstep_ = true;
  }
  lastDistance_ = dist;
  phase_ = std::fmod(phase_, 2.0f * kTwoPi);

  const float s = std::sin(phase_);
  const float w = moveWeight_;
  const float run = runWeight_;
  const float breathe = std::sin(time_ * 1.9f);

  // Pelvis height: bob twice per cycle, lower when crouching / landing.
  const float bob = w * (0.025f + 0.045f * run) * std::cos(2.0f * phase_);
  const float hipH = 0.94f - 0.34f * crouchWeight_ + bob - 0.12f * player.landingImpact() + 0.004f * breathe * (1 - w);
  const Vec3 pos = player.position();

  prevPos_ = pos_;
  prevYaw_ = yaw_;
  pos_ = pos;
  yaw_ = player.facingYaw();
  const Mat4 root = Mat4::translation(pos) * Mat4::rotationY(player.facingYaw());
  root_ = root;
  const Mat4 pelvis = root * Mat4::translation({0, hipH, 0}) * Mat4::rotationZ(leanSide_ * 0.5f) *
                      Mat4::rotationY(0.12f * w * s);
  const float torsoLean = leanForward_ + 0.32f * crouchWeight_ + 0.08f * airWeight_;
  const Mat4 chest = pelvis * Mat4::rotationY(-0.2f * w * s) * swingX(torsoLean) * Mat4::rotationZ(leanSide_ * 0.6f);

  auto& P = parts_;
  auto set = [&](BodyPart p, const Mat4& m, PartShape sh, PartMaterial mat) {
    P[static_cast<int>(p)] = {m, sh, mat};
  };

  set(BodyPart::Pelvis, pelvis * Mat4::translation({0, 0.02f, 0}) * Mat4::scale({0.17f, 0.13f, 0.11f}), PartShape::Box,
      PartMaterial::Trousers);
  // Torso: box from pelvis up to the shoulders (unit box is centred, so offset by half).
  set(BodyPart::Torso, chest * Mat4::translation({0, 0.29f, 0}) * Mat4::scale({0.2f + 0.003f * breathe, 0.26f, 0.13f}),
      PartShape::Box, PartMaterial::Jacket);
  const Mat4 neck = chest * Mat4::translation({0, 0.58f, 0.01f}) * swingX(-torsoLean * 0.6f + 0.05f * std::sin(time_ * 0.7f) * (1 - w));
  set(BodyPart::Head, neck * Mat4::translation({0, 0.12f, 0.01f}) * Mat4::scale({0.095f, 0.115f, 0.105f}),
      PartShape::Sphere, PartMaterial::Skin);
  // Wide straw hat: a flattened cone, iconic silhouette from behind.
  set(BodyPart::Hat, neck * Mat4::translation({0, 0.2f, 0.0f}) * Mat4::scale({0.3f, 0.11f, 0.3f}), PartShape::Cone,
      PartMaterial::Straw);
  set(BodyPart::Backpack, chest * Mat4::translation({0, 0.32f, -0.2f}) * Mat4::scale({0.16f, 0.22f, 0.09f}),
      PartShape::Box, PartMaterial::Canvas);
  set(BodyPart::Bedroll, chest * Mat4::translation({0, 0.6f, -0.2f}) * Mat4::rotationZ(kPi * 0.5f) *
      Mat4::translation({0, 0.2f, 0}) * Mat4::scale({0.075f, 0.4f, 0.075f}), PartShape::Capsule, PartMaterial::Leather);

  // Legs.
  const float thighAmp = (0.38f + 0.42f * run) * w;
  const float kneeAmp = (0.55f + 0.85f * run) * w;
  for (int side = 0; side < 2; ++side) {
    const float ph = phase_ + (side ? kPi : 0.0f);
    const float sw = std::sin(ph);
    float thigh = thighAmp * sw + 0.95f * crouchWeight_ + airWeight_ * (side ? 0.25f : 0.75f);
    float knee = 0.08f + kneeAmp * std::fmax(0.0f, std::sin(ph + 1.9f)) + 1.55f * crouchWeight_ +
                 airWeight_ * (side ? 0.5f : 1.1f);
    const float ankle = -0.25f * w * std::sin(ph + 0.6f) - 0.6f * crouchWeight_ * 0.5f;
    const float x = side ? -0.095f : 0.095f;
    const Mat4 hip = pelvis * Mat4::translation({x, -0.03f, 0}) * swingX(thigh) * Mat4::rotationZ(side ? 0.03f : -0.03f);
    const Mat4 kneeJ = hip * Mat4::translation({0, -0.45f, 0}) * swingX(-knee);
    const Mat4 ankleJ = kneeJ * Mat4::translation({0, -0.44f, 0}) * swingX(ankle);
    set(side ? BodyPart::ThighR : BodyPart::ThighL, limb(hip, 0.075f, 0.47f), PartShape::Capsule, PartMaterial::Trousers);
    set(side ? BodyPart::ShinR : BodyPart::ShinL, limb(kneeJ, 0.06f, 0.46f), PartShape::Capsule, PartMaterial::Trousers);
    set(side ? BodyPart::FootR : BodyPart::FootL,
        ankleJ * Mat4::translation({0, -0.035f, 0.055f}) * Mat4::scale({0.055f, 0.045f, 0.13f}), PartShape::Box,
        PartMaterial::Leather);
  }

  // Arms swing opposite to legs; elbows bend more when running.
  for (int side = 0; side < 2; ++side) {
    const float ph = phase_ + (side ? 0.0f : kPi);
    const float shoulder = (0.35f + 0.45f * run) * w * std::sin(ph) + 0.25f * crouchWeight_ - 0.5f * airWeight_;
    const float elbow = 0.25f + 1.05f * run * w + 0.3f * crouchWeight_ + 0.4f * airWeight_;
    const float x = side ? -0.25f : 0.25f;
    const float outward = (side ? -1.0f : 1.0f) * (0.08f + 0.35f * airWeight_ + 0.02f * breathe * (1 - w));
    const Mat4 sh = chest * Mat4::translation({x, 0.5f, 0}) * Mat4::rotationZ(outward) * swingX(shoulder);
    const Mat4 el = sh * Mat4::translation({0, -0.29f, 0}) * swingX(elbow);
    set(side ? BodyPart::UpperArmR : BodyPart::UpperArmL, limb(sh, 0.058f, 0.3f), PartShape::Capsule, PartMaterial::Jacket);
    set(side ? BodyPart::ForearmR : BodyPart::ForearmL, limb(el, 0.048f, 0.27f), PartShape::Capsule, PartMaterial::Jacket);
    set(side ? BodyPart::HandR : BodyPart::HandL, el * Mat4::translation({0, -0.31f, 0.0f}) * Mat4::scale({0.04f, 0.06f, 0.05f}),
        PartShape::Sphere, PartMaterial::Skin);
  }
}

}  // namespace aaa
