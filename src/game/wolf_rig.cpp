#include "game/wolf_rig.h"

#include <algorithm>
#include <cmath>

#include "game/wildlife.h"
#include "world/world.h"

namespace aaa {
namespace {

const SkinMaterial kFur = SkinMaterial::make({0.150f, 0.135f, 0.115f}, 0.95f, 160.0f, 0.45f);
const SkinMaterial kFurDark = SkinMaterial::make({0.060f, 0.052f, 0.045f}, 0.95f, 160.0f, 0.40f);
const SkinMaterial kFurLight = SkinMaterial::make({0.245f, 0.225f, 0.195f}, 0.95f, 150.0f, 0.42f);
const SkinMaterial kNose = SkinMaterial::make({0.030f, 0.026f, 0.026f}, 0.45f);
const SkinMaterial kEye = SkinMaterial::make({0.55f, 0.42f, 0.12f}, 0.30f, 0.0f, 0.0f, 2.5f);

}  // namespace

void WolfRig::build(int detail) {
  if (skeleton_.count() > 0) return;
  // Bones MUST be added in joint-enum order (a joint index is a bone index).
  auto B = [&](const char* name, int parent, Vec3 off) { return skeleton_.addBone(name, parent, off); };
  B("pelvis", -1, {0.0f, 0.63f, -0.34f});                          // 0  WJ_Pelvis
  B("spine1", WJ_Pelvis, {0.0f, 0.005f, 0.22f});                    // 1  WJ_Spine1
  B("spine2", WJ_Spine1, {0.0f, 0.0f, 0.22f});                      // 2  WJ_Spine2
  B("chest", WJ_Spine2, {0.0f, -0.01f, 0.18f});                     // 3  WJ_Chest
  B("neck1", WJ_Chest, {0.0f, 0.05f, 0.10f});                       // 4  WJ_Neck1
  B("neck2", WJ_Neck1, {0.0f, 0.05f, 0.16f});                       // 5  WJ_Neck2
  B("head", WJ_Neck2, {0.0f, 0.01f, 0.16f});                        // 6  WJ_Head
  B("jaw", WJ_Head, {0.0f, -0.045f, 0.035f});                       // 7  WJ_Jaw
  B("earL", WJ_Head, {0.055f, 0.075f, -0.03f});                     // 8  WJ_EarL
  B("earR", WJ_Head, {-0.055f, 0.075f, -0.03f});                    // 9  WJ_EarR
  B("tail1", WJ_Pelvis, {0.0f, 0.06f, -0.12f});                     // 10 WJ_Tail1
  B("tail2", WJ_Tail1, {0.0f, -0.02f, -0.09f});                     // 11 WJ_Tail2
  B("tail3", WJ_Tail2, {0.0f, -0.02f, -0.09f});                     // 12 WJ_Tail3
  B("tail4", WJ_Tail3, {0.0f, -0.02f, -0.09f});                     // 13 WJ_Tail4
  B("frontUpperL", WJ_Chest, {0.10f, -0.05f, 0.10f});               // 14 WJ_FrontUpperL
  B("frontLowerL", WJ_FrontUpperL, {0.0f, -0.29f, 0.0f});           // 15 WJ_FrontLowerL
  B("frontPawL", WJ_FrontLowerL, {0.0f, -0.30f, 0.0f});             // 16 WJ_FrontPawL
  B("frontUpperR", WJ_Chest, {-0.10f, -0.05f, 0.10f});              // 17 WJ_FrontUpperR
  B("frontLowerR", WJ_FrontUpperR, {0.0f, -0.29f, 0.0f});           // 18 WJ_FrontLowerR
  B("frontPawR", WJ_FrontLowerR, {0.0f, -0.30f, 0.0f});             // 19 WJ_FrontPawR
  B("hindUpperL", WJ_Pelvis, {0.10f, -0.06f, -0.04f});              // 20 WJ_HindUpperL
  B("hindLowerL", WJ_HindUpperL, {0.0f, -0.27f, 0.05f});            // 21 WJ_HindLowerL
  B("hindPawL", WJ_HindLowerL, {0.0f, -0.28f, -0.03f});             // 22 WJ_HindPawL
  B("hindUpperR", WJ_Pelvis, {-0.10f, -0.06f, -0.04f});             // 23 WJ_HindUpperR
  B("hindLowerR", WJ_HindUpperR, {0.0f, -0.27f, 0.05f});            // 24 WJ_HindLowerR
  B("hindPawR", WJ_HindLowerR, {0.0f, -0.28f, -0.03f});             // 25 WJ_HindPawR
  skeleton_.finalize();
  buildMesh(detail);
  pose_.resize(WJ_Count);
  for (int i = 0; i < WJ_Count; ++i) pose_.world(i) = skeleton_.bindWorld(i);
  pose_.computeSkin(skeleton_);
  prev_ = pose_;
}

void WolfRig::buildMesh(int detail) {
  const int sides = detail >= 2 ? 14 : (detail == 1 ? 11 : 8);
  auto world = [&](int j) { return skeleton_.bindWorld(j).position(); };
  SkinnedMesh& m = mesh_;
  m.vertices.clear();
  m.indices.clear();

  // Barrel body: rear -> front with a slight taper, plus chest and haunch masses.
  addSkinSweep(m, skeleton_,
               {world(WJ_Pelvis) + Vec3{0, -0.02f, -0.04f}, world(WJ_Spine1), world(WJ_Spine2), world(WJ_Chest)},
               {0.150f, 0.165f, 0.170f, 0.165f}, sides, kFur, true, false);
  addSkinEllipsoid(m, skeleton_, world(WJ_Pelvis) + Vec3{0, 0.0f, -0.06f}, {0.155f, 0.145f, 0.19f}, sides, 7, kFur);
  addSkinEllipsoid(m, skeleton_, world(WJ_Chest) + Vec3{0, 0.0f, 0.04f}, {0.155f, 0.150f, 0.20f}, sides, 7, kFur);
  // Saddle (darker guard hair along the spine) and a pale belly.
  addSkinEllipsoid(m, skeleton_, world(WJ_Spine2) + Vec3{0, 0.075f, 0.02f}, {0.115f, 0.055f, 0.26f}, sides, 5,
                   kFurDark);
  addSkinEllipsoid(m, skeleton_, world(WJ_Spine1) + Vec3{0, -0.115f, 0.03f}, {0.115f, 0.055f, 0.22f}, sides, 5,
                   kFurLight);
  // Neck, head, muzzle, jaw.
  addSkinSweep(m, skeleton_, {world(WJ_Chest) + Vec3{0, 0.03f, 0.09f}, world(WJ_Neck1), world(WJ_Neck2), world(WJ_Head)},
               {0.115f, 0.105f, 0.095f, 0.090f}, sides, kFur, false, false);
  addSkinEllipsoid(m, skeleton_, world(WJ_Head) + Vec3{0, 0.01f, 0.01f}, {0.095f, 0.090f, 0.110f}, sides, 7, kFur);
  addSkinSweep(m, skeleton_, {world(WJ_Head) + Vec3{0, -0.01f, 0.06f}, world(WJ_Head) + Vec3{0, -0.02f, 0.20f}},
               {0.055f, 0.042f}, sides, kFurDark, false, true);
  addSkinEllipsoid(m, skeleton_, world(WJ_Jaw) + Vec3{0, 0.0f, 0.075f}, {0.048f, 0.030f, 0.085f}, sides, 5,
                   kFurDark);
  addSkinEllipsoid(m, skeleton_, world(WJ_Head) + Vec3{0, -0.015f, 0.215f}, {0.022f, 0.018f, 0.018f}, sides, 5, kNose);
  // Eyes.
  for (int side = 0; side < 2; ++side) {
    const float s = side == 0 ? 1.0f : -1.0f;
    addSkinEllipsoid(m, skeleton_, world(WJ_Head) + Vec3{0.048f * s, 0.03f, 0.095f}, {0.014f, 0.012f, 0.012f}, sides, 4,
                     kEye, WJ_Head);
    // Ears: flattened cones on the ear joints (procedural fur cue: a soft silhouette).
    const int ear = side == 0 ? WJ_EarL : WJ_EarR;
    addSkinCone(m, skeleton_, skeleton_.bindWorld(ear), 0.045f, 0.085f, sides, kFurDark, ear);
    addSkinEllipsoid(m, skeleton_, world(ear) + Vec3{0, 0.075f, 0.0f}, {0.040f, 0.030f, 0.022f}, sides, 4, kFurDark, ear);
  }
  // Legs: three segments each (upper, lower, paw) — digital paws are a plain box.
  for (int side = 0; side < 2; ++side) {
    const int upF = side == 0 ? WJ_FrontUpperL : WJ_FrontUpperR;
    const int loF = side == 0 ? WJ_FrontLowerL : WJ_FrontLowerR;
    const int paF = side == 0 ? WJ_FrontPawL : WJ_FrontPawR;
    addSkinSweep(m, skeleton_, {world(upF), world(loF), world(paF)}, {0.062f, 0.040f, 0.033f}, sides, kFur, false, false);
    addSkinBox(m, skeleton_, skeleton_.bindWorld(paF) * Mat4::translation({0, -0.02f, 0.03f}), {0.038f, 0.022f, 0.062f},
               kFurDark, -1);
    const int upH = side == 0 ? WJ_HindUpperL : WJ_HindUpperR;
    const int loH = side == 0 ? WJ_HindLowerL : WJ_HindLowerR;
    const int paH = side == 0 ? WJ_HindPawL : WJ_HindPawR;
    addSkinSweep(m, skeleton_, {world(upH), world(loH), world(paH)}, {0.075f, 0.042f, 0.033f}, sides, kFur, false,
                 false);
    addSkinBox(m, skeleton_, skeleton_.bindWorld(paH) * Mat4::translation({0, -0.02f, 0.03f}), {0.038f, 0.022f, 0.058f},
               kFurDark, -1);
  }
  // Tail: a tapering sweep along the chain, then a dark tip.
  addSkinSweep(m, skeleton_,
               {world(WJ_Tail1), world(WJ_Tail2), world(WJ_Tail3), world(WJ_Tail4), world(WJ_Tail4) + Vec3{0, -0.06f, -0.06f}},
               {0.060f, 0.050f, 0.040f, 0.032f, 0.022f}, sides, kFur, false, true);
  m.computeBounds();
}

void WolfRig::reset(const Wolf& wolf, const World& world) {
  const Vec2 fwd{std::sin(wolf.yaw), std::cos(wolf.yaw)};
  const Vec2 right{fwd.y, -fwd.x};
  for (int i = 0; i < 4; ++i) {
    const float s = (i % 2 == 0) ? 1.0f : -1.0f;
    const float f = (i < 2) ? 0.30f : -0.32f;  // front / hind
    const Vec2 xz = wolf.pos.xz() + fwd * f + right * (0.10f * s);
    plant_[i] = Vec3{xz.x, world.groundHeight(xz.x, xz.y), xz.y};
    lift_[i] = plant_[i];
    nextPlant_[i] = plant_[i];
    stance_[i] = true;
    wasStance_[i] = true;
  }
  phase_ = std::fmod(wolf.gaitPhase * kTwoPi, kTwoPi);
  distance_ = phase_ / kTwoPi * 0.85f;
  time_ = 0.0f;
  // The pose must be in world space from the start (the mesh is generated in bind space).
  const Mat4 root = Mat4::translation(wolf.pos) * Mat4::rotationY(wolf.yaw);
  for (int i = 0; i < WJ_Count; ++i) pose_.world(i) = root * skeleton_.bindWorld(i);
  pose_.computeSkin(skeleton_);
  prev_ = pose_;
  prevFootWorld_[0] = pose_.world(WJ_FrontPawL).position();
  prevFootWorld_[1] = pose_.world(WJ_FrontPawR).position();
  prevFootWorld_[2] = pose_.world(WJ_HindPawL).position();
  prevFootWorld_[3] = pose_.world(WJ_HindPawR).position();
  maxFootDrift_ = 0.0f;
  skipDriftStep_ = true;
  initialised_ = true;
}

float WolfRig::groundAt(const World& world, Vec2 xz) const { return world.groundHeight(xz.x, xz.y); }

void WolfRig::update(float dt, const Wolf& wolf, const World& world) {
  const float step = clampf(dt, 0.0f, 0.1f);
  time_ += step;
  if (!initialised_) reset(wolf, world);
  prev_ = pose_;  // render interpolation blends prev -> current (PROMPT §8.6)

  const float speed = std::fmax(wolf.speed, 0.0f);
  const float accel = (speed - prevSpeed_) / std::fmax(step, 1e-4f);
  prevSpeed_ = speed;
  gaitW_ = lerp(gaitW_, smoothstep(0.05f, 1.0f, speed), dampFactor(0.10f, step));
  lopeW_ = lerp(lopeW_, smoothstep(3.0f, 7.5f, speed), dampFactor(0.14f, step));
  stalkW_ = lerp(stalkW_, wolf.state == WolfState::Stalk ? 1.0f : 0.0f, dampFactor(0.35f, step));
  fleeW_ = lerp(fleeW_, (wolf.state == WolfState::Retreat || wolf.state == WolfState::Charge) ? 1.0f : 0.0f,
                dampFactor(0.30f, step));

  // Gait phase from the distance actually travelled (no foot sliding).
  const float stride = lerp(0.85f, 1.9f, lopeW_);
  distance_ += speed * step;
  float advance = speed * step;
  if (gaitW_ < 0.35f && (!stance_[0] || !stance_[1] || !stance_[2] || !stance_[3]))
    advance = std::fmax(advance, 0.9f * step);  // finish an interrupted step
  phase_ += advance / std::fmax(stride, 0.3f) * kTwoPi;
  phase_ = std::fmod(phase_, kTwoPi);
  if (phase_ < 0.0f) phase_ += kTwoPi;

  const float breathe = std::sin(time_ * 2.2f + static_cast<float>(wolf.rng % 7)) * 0.006f;
  const float bob = std::fabs(std::sin(phase_)) * 0.030f * gaitW_;
  const Vec2 fwd2{std::sin(wolf.yaw), std::cos(wolf.yaw)};
  const Vec2 right2{fwd2.y, -fwd2.x};

  // --- feet: diagonal trot pairs, planted during stance ---------------------------
  struct LegDef {
    int upper, lower, paw;
    float lateral, fore;
    float offset;  // phase offset (trot = diagonal pairs together)
    bool front;
  };
  const LegDef legs[4] = {
      {WJ_FrontUpperL, WJ_FrontLowerL, WJ_FrontPawL, 0.10f, 0.30f, 0.0f, true},
      {WJ_FrontUpperR, WJ_FrontLowerR, WJ_FrontPawR, -0.10f, 0.30f, 0.5f, true},
      {WJ_HindUpperL, WJ_HindLowerL, WJ_HindPawL, 0.10f, -0.32f, 0.5f, false},
      {WJ_HindUpperR, WJ_HindLowerR, WJ_HindPawR, -0.10f, -0.32f, 0.0f, false},
  };
  Vec3 footTarget[4];
  for (int i = 0; i < 4; ++i) {
    const float ph = std::fmod(phase_ + legs[i].offset * kTwoPi, kTwoPi);
    const bool stance = ph < kPi;
    const Vec2 rest = wolf.pos.xz() + fwd2 * legs[i].fore + right2 * legs[i].lateral;
    if (stance != stance_[i]) {
      if (!stance) lift_[i] = plant_[i];
      else if (speed > 0.15f) plant_[i] = nextPlant_[i];
      else plant_[i] = Vec3{rest.x, groundAt(world, rest), rest.y};
      stance_[i] = stance;
    }
    if (stance || gaitW_ < 0.05f) {
      footTarget[i] = Vec3{plant_[i].x, groundAt(world, plant_[i].xz()), plant_[i].z};
    } else {
      const float t = smoothstep(0.0f, 1.0f, (ph - kPi) / kPi);
      const Vec2 next = rest + fwd2 * (stride * 0.30f);
      const Vec2 from = lift_[i].xz();
      const Vec2 xz = from + (next - from) * t;
      const float gy = groundAt(world, xz);
      const float arc = std::sin(t * kPi) * (0.05f + 0.09f * lopeW_);
      footTarget[i] = Vec3{xz.x, gy + arc, xz.y};
      nextPlant_[i] = Vec3{xz.x, gy, xz.y};
    }
  }

  // --- spine: flex with the gait, crouch when stalking, stretch when fleeing -------
  const float flex = std::sin(phase_ * 2.0f) * (0.035f + 0.05f * lopeW_) * gaitW_;
  const float stretch = 0.10f * fleeW_ + clampf(accel * 0.01f, -0.05f, 0.05f);
  // The rig's origin is on the ground; the pelvis bone hangs above and behind it (its
  // bind offset), so the body sits at the right shoulder height.
  Mat4 pelvis = Mat4::translation(wolf.pos + Vec3{0, bob + breathe - 0.05f * stalkW_, 0}) * Mat4::rotationY(wolf.yaw) *
                Mat4::rotationX(-flex * 0.5f + stretch) * Mat4::translation(skeleton_.bone(WJ_Pelvis).bindOffset);
  pose_.world(WJ_Pelvis) = pelvis;
  auto chain = [&](int joint, const Mat4& parent, float yaw, float pitch) {
    Mat4 j = parent * Mat4::translation(skeleton_.bone(joint).bindOffset) * Mat4::rotationY(yaw) * Mat4::rotationX(pitch);
    pose_.world(joint) = j;
    return j;
  };
  Mat4 s1 = chain(WJ_Spine1, pelvis, 0.0f, flex * 0.9f);
  Mat4 s2 = chain(WJ_Spine2, s1, 0.0f, flex * 0.6f);
  Mat4 chest = chain(WJ_Chest, s2, 0.0f, -flex * 0.5f - 0.06f * stalkW_);
  // Neck/head: low and forward when stalking or fleeing, level otherwise.
  const float headDrop = 0.34f * stalkW_ + 0.10f * fleeW_;
  Mat4 n1 = chain(WJ_Neck1, chest, 0.0f, headDrop * 0.5f + flex * -0.3f);
  Mat4 n2 = chain(WJ_Neck2, n1, 0.0f, headDrop * 0.4f);
  Mat4 head = chain(WJ_Head, n2, 0.0f, headDrop * 0.3f - 0.05f * gaitW_);
  pose_.world(WJ_Head) = head;
  chain(WJ_Jaw, head, 0.0f, 0.06f + 0.10f * fleeW_ + 0.05f * std::sin(time_ * 3.0f) * fleeW_);

  // --- secondary motion: ear + tail springs (damped, driven by acceleration) ------
  const float accelDrive = clampf(accel * 0.6f, -1.0f, 1.0f);
  for (int i = 0; i < 2; ++i) {
    // Ears lag behind head motion: a simple spring toward rest, kicked by accel/turn.
    const float drive = -accelDrive * 0.20f + 0.18f * stalkW_ + 0.25f * std::sin(time_ * 2.3f + i * 1.7f) * (1.0f - gaitW_);
    earSpring_[i] += (drive - earSpring_[i]) * dampFactor(0.09f, step);
    const int ear = i == 0 ? WJ_EarL : WJ_EarR;
    pose_.world(ear) = head * Mat4::translation(skeleton_.bone(ear).bindOffset) *
                       Mat4::rotationX(-0.10f - 0.22f * stalkW_ + earSpring_[i]) *
                       Mat4::rotationZ((i == 0 ? 1.0f : -1.0f) * (0.06f + 0.10f * std::sin(time_ * 1.7f + i)));
  }
  // Tail: a three-stage spring chain; hangs low when stalking, streams when running.
  float tailPitch[4];
  const float wag = std::sin(time_ * (wolf.state == WolfState::Roam ? 3.4f : 1.2f)) * (0.10f + 0.10f * gaitW_);
  const float base = 0.25f * stalkW_ - 0.30f * fleeW_ - 0.10f * gaitW_;
  float drive = base + accelDrive * 0.18f;
  for (int i = 0; i < 4; ++i) {
    if (i < 3) {
      tailSpring_[i] += (drive - tailSpring_[i]) * dampFactor(0.10f + 0.05f * static_cast<float>(i), step);
      drive = tailSpring_[i] * 0.85f;
    }
    tailPitch[i] = (i < 3 ? tailSpring_[i] : tailSpring_[2] * 0.8f) + wag * (0.4f + 0.2f * static_cast<float>(i));
  }
  Mat4 tailParent = pelvis;
  const int tailJoints[4] = {WJ_Tail1, WJ_Tail2, WJ_Tail3, WJ_Tail4};
  for (int i = 0; i < 4; ++i) {
    tailParent = tailParent * Mat4::translation(skeleton_.bone(tailJoints[i]).bindOffset) *
                 Mat4::rotationX(tailPitch[i]) * Mat4::rotationY(wag * 0.5f);
    pose_.world(tailJoints[i]) = tailParent;
  }

  // --- legs: two-bone IK with planted paws ---------------------------------------
  for (int i = 0; i < 4; ++i) {
    const LegDef& L = legs[i];
    const Vec3 hip = (L.front ? pose_.world(WJ_Chest) : pelvis).transformPoint(skeleton_.bone(L.upper).bindOffset);
    const float upperLen = length(skeleton_.bone(L.lower).bindOffset);
    const float lowerLen = length(skeleton_.bone(L.paw).bindOffset);
    // Quadruped legs bend in the sagittal plane: the front (elbow) points back, the hind
    // (stifle) points forward. A fixed plane keeps a swinging paw from flipping its knee.
    const Vec3 lateral{-fwd2.y, 0.0f, fwd2.x};
    const IkResult ik = solveTwoBoneIkPlanar(hip, footTarget[i], upperLen, lowerLen, lateral, L.front ? 1.0f : -1.0f);
    const Vec3 face{fwd2.x, 0.0f, fwd2.y};
    pose_.world(L.upper) = aimJointStable(hip, ik.jointPos, face);
    pose_.world(L.lower) = aimJointStable(ik.jointPos, ik.endPos, face);
    pose_.world(L.paw) = aimJointStable(ik.endPos, ik.endPos + Vec3{0, -0.2f, 0.02f}, face);
  }

  // Diagnostics: planted paw drift (§10.12).
  if (skipDriftStep_) skipDriftStep_ = false;
  for (int i = 0; i < 4 && !skipDriftStep_; ++i) {
    const Vec3 w = pose_.world(legs[i].paw).position();
    if (stance_[i] && wasStance_[i] && speed > 0.2f)
      maxFootDrift_ = std::fmax(maxFootDrift_, length(w - prevFootWorld_[i]));
    prevFootWorld_[i] = w;
    wasStance_[i] = stance_[i];
  }
  pose_.computeSkin(skeleton_);
  if (!pose_.finite()) {
    for (int i = 0; i < WJ_Count; ++i) pose_.world(i) = skeleton_.bindWorld(i);
    pose_.computeSkin(skeleton_);
  }
}

void WolfRig::fillSkinPalette(float alpha, std::vector<Mat4>& out) const {
  out.resize(static_cast<size_t>(pose_.count()));
  const float t = clampf(alpha, 0.0f, 1.0f);
  for (size_t i = 0; i < out.size(); ++i) {
    const Mat4& a = prev_.skin()[i];
    const Mat4& b = pose_.skin()[i];
    Mat4 r;
    for (int k = 0; k < 16; ++k) r.m[k] = a.m[k] + (b.m[k] - a.m[k]) * t;
    out[i] = r;
  }
}

}  // namespace aaa
