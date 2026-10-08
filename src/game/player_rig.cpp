#include "game/player_rig.h"

#include <algorithm>
#include <cmath>

#include "game/player.h"
#include "core/log.h"
#include "world/world.h"

namespace aaa {
namespace {

// Muted, travel-worn palette (linear albedo) — matches the previous rigid stand-in so
// the character keeps its identity (PROMPT §2 "preserve the existing visual character").
const SkinMaterial kSkin = SkinMaterial::make({0.42f, 0.27f, 0.19f}, 0.55f);
const SkinMaterial kJacket = SkinMaterial::make({0.11f, 0.14f, 0.15f}, 0.85f, 90.0f, 0.18f);
const SkinMaterial kTrousers = SkinMaterial::make({0.16f, 0.13f, 0.10f}, 0.90f, 70.0f, 0.15f);
const SkinMaterial kLeather = SkinMaterial::make({0.12f, 0.07f, 0.04f}, 0.60f);
const SkinMaterial kCanvas = SkinMaterial::make({0.25f, 0.21f, 0.15f}, 0.90f, 55.0f, 0.25f);
const SkinMaterial kStraw = SkinMaterial::make({0.38f, 0.30f, 0.16f}, 0.85f, 120.0f, 0.35f);
const SkinMaterial kWrap = SkinMaterial::make({0.30f, 0.13f, 0.10f}, 0.88f, 80.0f, 0.2f);
const SkinMaterial kHair = SkinMaterial::make({0.06f, 0.05f, 0.04f}, 0.95f, 200.0f, 0.5f);

// Terrain normal from finite differences of the ground height.
Vec3 terrainNormal(const World& world, Vec2 xz, float eps) {
  const float hx = world.groundHeight(xz.x + eps, xz.y) - world.groundHeight(xz.x - eps, xz.y);
  const float hz = world.groundHeight(xz.x, xz.y + eps) - world.groundHeight(xz.x, xz.y - eps);
  return normalize(Vec3{-hx / (2.0f * eps), 1.0f, -hz / (2.0f * eps)});
}

float smoothstep01(float t) { return smoothstep(0.0f, 1.0f, clampf(t, 0.0f, 1.0f)); }

}  // namespace

// ---------------------------------------------------------------------------
// Skeleton + mesh
// ---------------------------------------------------------------------------
void PlayerRig::build(int detail) {
  if (skeleton_.count() > 0) return;  // built once per quality level, then cached
  // Bones MUST be added in joint-enum order: a joint index is a bone index (the pose
  // writes pose_.world(joint) and the mesh weights name joints), so any other order
  // silently skins the mesh to the wrong bones.
  auto B = [&](const char* name, int parent, Vec3 off) { return skeleton_.addBone(name, parent, off); };
  B("pelvis", -1, {0.0f, metrics_.hipHeight, 0.0f});              // 0  PJ_Pelvis
  B("spine1", PJ_Pelvis, {0.0f, 0.12f, 0.0f});                    // 1  PJ_Spine1
  B("spine2", PJ_Spine1, {0.0f, 0.12f, 0.0f});                    // 2  PJ_Spine2
  B("chest", PJ_Spine2, {0.0f, 0.12f, 0.0f});                     // 3  PJ_Chest
  B("neck", PJ_Chest, {0.0f, 0.16f, 0.01f});                      // 4  PJ_Neck
  B("head", PJ_Neck, {0.0f, 0.12f, 0.01f});                       // 5  PJ_Head
  B("clavL", PJ_Chest, {0.06f, 0.14f, 0.0f});                     // 6  PJ_ClavL
  B("upperArmL", PJ_ClavL, {0.19f, -0.02f, 0.0f});                // 7  PJ_UpperArmL
  B("lowerArmL", PJ_UpperArmL, {0.0f, -0.29f, 0.0f});             // 8  PJ_LowerArmL
  B("handL", PJ_LowerArmL, {0.0f, -0.27f, 0.0f});                 // 9  PJ_HandL
  B("clavR", PJ_Chest, {-0.06f, 0.14f, 0.0f});                    // 10 PJ_ClavR
  B("upperArmR", PJ_ClavR, {-0.19f, -0.02f, 0.0f});               // 11 PJ_UpperArmR
  B("lowerArmR", PJ_UpperArmR, {0.0f, -0.29f, 0.0f});             // 12 PJ_LowerArmR
  B("handR", PJ_LowerArmR, {0.0f, -0.27f, 0.0f});                 // 13 PJ_HandR
  B("thighL", PJ_Pelvis, {0.095f, -0.03f, 0.0f});                 // 14 PJ_ThighL
  B("calfL", PJ_ThighL, {0.0f, -metrics_.thigh, 0.0f});           // 15 PJ_CalfL
  B("footL", PJ_CalfL, {0.0f, -metrics_.calf, 0.0f});             // 16 PJ_FootL
  B("toeL", PJ_FootL, {0.0f, -0.06f, 0.14f});                     // 17 PJ_ToeL
  B("thighR", PJ_Pelvis, {-0.095f, -0.03f, 0.0f});                // 18 PJ_ThighR
  B("calfR", PJ_ThighR, {0.0f, -metrics_.thigh, 0.0f});           // 19 PJ_CalfR
  B("footR", PJ_CalfR, {0.0f, -metrics_.calf, 0.0f});             // 20 PJ_FootR
  B("toeR", PJ_FootR, {0.0f, -0.06f, 0.14f});                     // 21 PJ_ToeR
  skeleton_.finalize();
  buildMesh(detail);
  pose_.resize(PJ_Count);
  prev_.resize(PJ_Count);
  // Bind pose as the initial pose so nothing is undefined before the first update.
  for (int i = 0; i < PJ_Count; ++i) pose_.world(i) = skeleton_.bindWorld(i);
  pose_.computeSkin(skeleton_);
  prev_ = pose_;
}

void PlayerRig::buildMesh(int detail) {
  const int sides = detail >= 2 ? 14 : (detail == 1 ? 11 : 8);
  SkinnedMesh& m = mesh_;
  m.vertices.clear();
  m.indices.clear();

  auto world = [&](int joint) { return skeleton_.bindWorld(joint).position(); };

  // Hips + torso (jacket over a hemp shirt).
  addSkinSweep(m, skeleton_, {world(PJ_Pelvis) + Vec3{0, -0.02f, 0}, world(PJ_Spine1), world(PJ_Spine2), world(PJ_Chest)},
               {0.155f, 0.150f, 0.158f, 0.150f}, sides, kJacket, true, false);
  addSkinEllipsoid(m, skeleton_, world(PJ_Pelvis) + Vec3{0, 0.0f, 0}, {0.165f, 0.115f, 0.115f}, sides, 7, kTrousers);
  // Waist wrap (sash) — a little accent of colour.
  addSkinEllipsoid(m, skeleton_, world(PJ_Pelvis) + Vec3{0, 0.13f, 0}, {0.16f, 0.055f, 0.12f}, sides, 5, kWrap,
                   PJ_Pelvis);
  // Shoulders (so the torso->arm transition reads smoothly).
  for (int side = 0; side < 2; ++side) {
    const int clav = side == 0 ? PJ_ClavL : PJ_ClavR;
    addSkinEllipsoid(m, skeleton_, world(clav) + Vec3{0.06f * (side == 0 ? 1.0f : -1.0f), 0.01f, 0},
                     {0.075f, 0.075f, 0.085f}, sides, 6, kJacket);
  }
  // Neck + head (+ hair cap).
  addSkinSweep(m, skeleton_, {world(PJ_Neck), world(PJ_Head)}, {0.052f, 0.055f}, sides, kSkin, false, false);
  addSkinEllipsoid(m, skeleton_, world(PJ_Head) + Vec3{0, 0.02f, 0.005f}, {0.090f, 0.112f, 0.100f}, sides, 8, kSkin);
  addSkinEllipsoid(m, skeleton_, world(PJ_Head) + Vec3{0, 0.075f, -0.01f}, {0.088f, 0.060f, 0.095f}, sides, 5, kHair,
                   PJ_Head);
  // Wide straw hat, rigidly attached to the head (PROMPT §10.3).
  {
    Mat4 hat = skeleton_.bindWorld(PJ_Head) * Mat4::translation({0.0f, 0.19f, 0.0f});
    addSkinCone(m, skeleton_, hat, 0.30f, 0.10f, sides * 2, kStraw, PJ_Head);
    addSkinEllipsoid(m, skeleton_, (hat * Mat4::translation({0, 0.03f, 0})).position(),
                     {0.115f, 0.075f, 0.115f}, sides, 5, kStraw, PJ_Head);
  }
  // Backpack + bedroll, rigidly attached to the chest.
  addSkinBox(m, skeleton_, skeleton_.bindWorld(PJ_Chest) * Mat4::translation({0.0f, 0.30f, -0.20f}),
             {0.15f, 0.20f, 0.085f}, kCanvas, PJ_Chest);
  addSkinBox(m, skeleton_, skeleton_.bindWorld(PJ_Chest) * Mat4::translation({0.0f, 0.56f, -0.19f}),
             {0.17f, 0.055f, 0.075f}, kLeather, PJ_Chest);

  // Arms and legs (sweeps follow the bind chain, so weights blend smoothly).
  for (int side = 0; side < 2; ++side) {
    const int clav = side == 0 ? PJ_ClavL : PJ_ClavR;
    const int upper = side == 0 ? PJ_UpperArmL : PJ_UpperArmR;
    const int lower = side == 0 ? PJ_LowerArmL : PJ_LowerArmR;
    const int hand = side == 0 ? PJ_HandL : PJ_HandR;
    addSkinSweep(m, skeleton_, {world(clav), world(upper), world(lower), world(hand)},
                 {0.070f, 0.062f, 0.050f, 0.045f}, sides, kJacket, false, false);
    addSkinEllipsoid(m, skeleton_, world(hand) + Vec3{0, -0.035f, 0}, {0.042f, 0.062f, 0.052f}, sides, 5, kSkin);
    const int thigh = side == 0 ? PJ_ThighL : PJ_ThighR;
    const int calf = side == 0 ? PJ_CalfL : PJ_CalfR;
    const int foot = side == 0 ? PJ_FootL : PJ_FootR;
    addSkinSweep(m, skeleton_, {world(thigh), world(calf), world(foot)}, {0.092f, 0.062f, 0.050f}, sides, kTrousers,
                 false, false);
    // Boot: a rounded box under the ankle.
    addSkinBox(m, skeleton_, skeleton_.bindWorld(foot) * Mat4::translation({0.0f, -0.035f, 0.06f}),
               {0.055f, 0.042f, 0.125f}, kLeather, -1);
  }
  m.computeBounds();
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------
void PlayerRig::reset(const PlayerController& player, const World& world) {
  const Vec3 p = player.position();
  const Vec2 fwd{std::sin(player.facingYaw()), std::cos(player.facingYaw())};
  for (int i = 0; i < 2; ++i) {
    const float s = i == 0 ? 1.0f : -1.0f;
    const Vec2 xz = p.xz() + Vec2{fwd.x * 0.05f, fwd.y * 0.05f} + Vec2{fwd.y, -fwd.x} * (0.095f * s);
    plant_[i] = Vec3{xz.x, groundAt(world, xz), xz.y};
    lift_[i] = plant_[i];
    nextPlant_[i] = plant_[i];
    airFoot_[i] = plant_[i];
    footNormal_[i] = terrainNormal(world, Vec2{plant_[i].x, plant_[i].z}, 0.5f);
    stance_[i] = true;
    wasStance_[i] = true;
  }
  // Put the pose in world space straight away (the mesh is generated in bind/rest
  // space, so the first rendered frame must already be in the world).
  const Mat4 root = Mat4::translation(p) * Mat4::rotationY(player.facingYaw());
  for (int i = 0; i < PJ_Count; ++i) pose_.world(i) = root * skeleton_.bindWorld(i);
  pose_.computeSkin(skeleton_);
  prev_ = pose_;
  prevFootWorld_[0] = pose_.world(PJ_FootL).position();
  prevFootWorld_[1] = pose_.world(PJ_FootR).position();
  lastDistance_ = player.distanceTravelled();
  phase_ = 0.0f;
  time_ = 0.0f;
  maxFootDrift_ = 0.0f;
  skipDriftStep_ = true;
  initialised_ = true;
}

float PlayerRig::groundAt(const World& world, Vec2 xz) const {
  return world.groundHeight(xz.x, xz.y);
}

Vec3 PlayerRig::footPosition(int i) const {
  // The ankle joint is the point the IK plants, so it is what "foot drift" measures.
  return pose_.world(i == 0 ? PJ_FootL : PJ_FootR).position();
}

void PlayerRig::update(float dt, const PlayerController& player, const World& world, Vec3 cameraForward, float cold,
                       float fireHeat, PlayerGesture gesture, float gestureAge) {
  const float step = clampf(dt, 0.0f, 0.1f);
  time_ += step;
  if (!initialised_) reset(player, world);
  // Keep the previous step's pose: the renderer blends prev -> current by the
  // interpolation alpha (fixed-step simulation, PROMPT §8.6).
  prev_ = pose_;

  const float speed = player.horizontalSpeed();
  const float accel = (speed - prevSpeed_) / std::fmax(step, 1e-4f);
  prevSpeed_ = speed;
  const bool grounded = player.grounded();
  const float wade = smoothstep(0.02f, 0.40f, player.waterDepth());

  // Smoothed state weights (blended, never snapped: PROMPT §10.11).
  moveW_ = lerp(moveW_, smoothstep(0.12f, 1.10f, speed), dampFactor(0.09f, step));
  runW_ = lerp(runW_, smoothstep(2.0f, 6.0f, speed), dampFactor(0.16f, step));
  crouchW_ = lerp(crouchW_, player.crouching() ? 1.0f : 0.0f, dampFactor(0.09f, step));
  airW_ = lerp(airW_, grounded ? 0.0f : 1.0f, dampFactor(grounded ? 0.05f : 0.10f, step));
  wadeW_ = lerp(wadeW_, wade, dampFactor(0.25f, step));
  // Gesture envelope: quick in, slow out, so the pose reads but never pops.
  const float gTarget = (gesture != PlayerGesture::None && gestureAge < 1.6f) ? 1.0f : 0.0f;
  gestureW_ = lerp(gestureW_, gTarget, dampFactor(gTarget > 0.5f ? 0.06f : 0.22f, step));
  for (int gi = 0; gi < static_cast<int>(PlayerGesture::Rest) + 1; ++gi) {
    const bool active = gTarget > 0.5f && static_cast<int>(gesture) == gi;
    gesturePose_[gi] = lerp(gesturePose_[gi], active ? 1.0f : 0.0f, dampFactor(active ? 0.12f : 0.28f, step));
  }
  const int gIdx = static_cast<int>(gesture) < static_cast<int>(PlayerGesture::Rest) + 1 ? static_cast<int>(gesture) : 0;
  // Kneeling at a fire when idle, warm and close to it (PROMPT §10.8).
  const float kneelTarget = (grounded && speed < 0.35f && fireHeat > 0.55f) ? 1.0f : 0.0f;
  kneelW_ = lerp(kneelW_, kneelTarget, dampFactor(0.5f, step));
  // Shivering when cold.
  const float shiverTarget = grounded ? clampf(cold, 0.0f, 1.0f) * (1.0f - 0.6f * moveW_) : 0.0f;
  shiverW_ = lerp(shiverW_, shiverTarget, dampFactor(0.4f, step));

  leanForward_ = lerp(leanForward_, 0.05f * moveW_ + 0.17f * runW_ + clampf(accel * 0.02f, -0.12f, 0.12f),
                      dampFactor(0.12f, step));
  leanSide_ = lerp(leanSide_, clampf(-player.turnRate() * speed * 0.035f, -0.28f, 0.28f), dampFactor(0.10f, step));

  // --- gait phase (distance driven: no foot sliding) --------------------------------
  // One step per `strideLen` metres; the duty cycle shortens the stance at a run so the
  // feet are not asked to stay planted beyond the leg's reach (a run has a flight phase).
  const float strideLen = lerp(metrics_.stride, metrics_.stride * 1.75f, runW_) * lerp(1.0f, 0.62f, crouchW_) *
                          lerp(1.0f, 0.78f, wadeW_);
  const float duty = lerp(0.5f, 0.36f, runW_);
  const float dist = player.distanceTravelled();
  if (grounded) {
    float advance = dist - lastDistance_;
    // Never freeze mid-step: when the player stops with a foot in the air, let that
    // step finish (the stance foot never slides — it simply lands where it was going).
    if (moveW_ < 0.35f && (!stance_[0] || !stance_[1])) advance = std::fmax(advance, 0.9f * step);
    phase_ += advance / std::fmax(strideLen, 0.2f) * kPi;
  }
  lastDistance_ = dist;
  phase_ = std::fmod(phase_, kTwoPi);
  if (phase_ < 0.0f) phase_ += kTwoPi;

  // --- pelvis ---------------------------------------------------------------------
  const Vec3 p = player.position();
  const float yaw = player.facingYaw();
  const Vec2 fwd2{std::sin(yaw), std::cos(yaw)};
  const Vec2 right2{fwd2.y, -fwd2.x};
  const float breathe = std::sin(time_ * 1.9f);
  const float bob = moveW_ * (0.022f + 0.045f * runW_) * std::cos(2.0f * phase_);
  const float hipH = metrics_.hipHeight * (1.0f - 0.30f * crouchW_ - 0.42f * kneelW_) - 0.10f * player.landingImpact() +
                     bob + 0.006f * breathe * (1.0f - moveW_) + 0.16f * airW_;

  // Feet: stance feet stay planted where they landed; swing feet reach for the next
  // plant position ahead of the pelvis. Pelvis height follows the terrain underfoot.
  float footY[2];
  Vec3 footTarget[2];
  const float stanceEnd = kTwoPi * duty;
  for (int i = 0; i < 2; ++i) {
    const float s = i == 0 ? 1.0f : -1.0f;
    const float ph = std::fmod(phase_ + (i == 0 ? 0.0f : kPi), kTwoPi);
    const bool stance = ph < stanceEnd;
    const Vec2 restXZ = p.xz() + right2 * (0.095f * s) + fwd2 * (0.03f + 0.10f * crouchW_);
    if (stance != stance_[i]) {
      // Transition: remember where the foot left the ground, or plant it exactly where
      // the swing was heading (so a landing never slides or pops).
      if (!stance) lift_[i] = plant_[i];
      else if (speed > 0.15f) plant_[i] = nextPlant_[i];
      else plant_[i] = Vec3{restXZ.x, groundAt(world, restXZ), restXZ.y};
      stance_[i] = stance;
    }
    // --- grounded target: planted during stance, an arc during swing ------------------
    Vec3 groundTarget;
    if (stance) {
      const float gy = groundAt(world, plant_[i].xz());
      groundTarget = Vec3{plant_[i].x, gy, plant_[i].z};
    } else {
      const float t = smoothstep01((ph - stanceEnd) / (kTwoPi - stanceEnd));
      // The plant is placed half the stance travel ahead, so the leg stays in reach for
      // the whole stance (a run covers more ground and spends less of the cycle planted).
      const float stanceTravel = 2.0f * duty * strideLen;
      const Vec2 dest = p.xz() + right2 * (0.095f * s) + fwd2 * (stanceTravel * 0.5f);
      const Vec2 from = lift_[i].xz();
      const Vec2 xz = from + (dest - from) * t;
      const float gy = groundAt(world, xz);
      const float arc = std::sin(t * kPi) * (0.09f + 0.10f * runW_ + 0.18f * wadeW_);
      groundTarget = Vec3{xz.x, gy + arc, xz.y};
      // Where the foot is right now: if the step ends here, this is where it plants.
      nextPlant_[i] = Vec3{xz.x, gy, xz.y};
    }

    // --- airborne: the foot slides under the body and the leg reaches for the ground --
    // Both the airborne position and the blend weight are continuous, and the leg starts
    // extending as soon as the ground is within reach, so take-off and landing are smooth.
    const float lead = (i == 1) ? 0.20f : 0.02f;
    const Vec3 under{p.x + right2.x * (0.095f * s) + fwd2.x * lead, 0.0f,
                     p.z + right2.y * (0.095f * s) + fwd2.y * lead};
    if (grounded) {
      // Always track the grounded foot, so a take-off starts from the real push-off point.
      airFoot_[i] = Vec3{groundTarget.x, 0.0f, groundTarget.z};
    } else {
      const float damp = dampFactor(0.22f, step);
      airFoot_[i].x = lerp(airFoot_[i].x, under.x, damp);
      airFoot_[i].z = lerp(airFoot_[i].z, under.z, damp);
    }
    const float airGroundY = groundAt(world, Vec2{airFoot_[i].x, airFoot_[i].z});
    const float above = std::fmax(p.y - airGroundY, 0.0f);
    const float fade = clampf(above / 0.55f, 0.0f, 1.0f);
    const Vec3 hang{airFoot_[i].x, p.y + hipH - 0.30f - 0.06f * fade, airFoot_[i].z};
    const Vec3 airTarget = lerp(Vec3{airFoot_[i].x, airGroundY + metrics_.footDrop, airFoot_[i].z}, hang, fade);

    // Blend on real ground clearance: the legs stay planted through the first part of the
    // launch and are already reaching down before touchdown.
    const float footAirBlend = std::fmin(airW_, clampf((p.y - groundAt(world, p.xz())) / 0.45f, 0.0f, 1.0f));
    footTarget[i] = lerp(groundTarget, airTarget, footAirBlend);
    footY[i] = groundAt(world, footTarget[i].xz());
    if (airW_ > 0.02f) {
      // In the air the foot carries its landing point with it, so the landing itself is
      // the same as any other plant (and no stale plant is ever reused). A foot that was
      // still swinging keeps its real lift-off point, which keeps the swing trajectory
      // continuous for the rest of the flight.
      const bool wasStance = stance_[i];
      stance_[i] = false;
      plant_[i] = Vec3{footTarget[i].x, groundAt(world, footTarget[i].xz()), footTarget[i].z};
      nextPlant_[i] = plant_[i];
      if (wasStance) lift_[i] = plant_[i];
      airFoot_[i] = Vec3{plant_[i].x, 0.0f, plant_[i].z};
    }
  }
  // The pelvis is supported by the feet that are ON the ground: a swinging foot (or its
  // arc) must never lift the hips, or the body would bob with the step instead of with
  // the support.
  float footMidY = 0.0f;
  int supportFeet = 0;
  for (int i = 0; i < 2; ++i) {
    if (stance_[i] || airW_ > 0.02f) {
      footMidY += footY[i];
      ++supportFeet;
    }
  }
  if (supportFeet > 0) footMidY /= static_cast<float>(supportFeet);
  else footMidY = (footY[0] + footY[1]) * 0.5f;
  const float legLen = metrics_.thigh + metrics_.calf;
  // Pelvis height follows the stance feet: the stance offset sets how far the hips can
  // sit above the ground with the leg still reaching (this is what produces the natural
  // two-per-cycle bob, and it levels the pelvis out on slopes).
  float stanceOffset = 0.0f;
  bool anyStance = false;
  for (int i = 0; i < 2; ++i) {
    if (!stance_[i] && moveW_ > 0.05f) continue;
    anyStance = true;
    // Measured from the hip's nominal position (the leg's real root), not the pelvis
    // centre, so the sideways stance width is included in the reach calculation.
    const float s = i == 0 ? 1.0f : -1.0f;
    const Vec2 hipXZ = p.xz() + right2 * (0.095f * s);
    stanceOffset = std::fmax(stanceOffset, length(footTarget[i].xz() - hipXZ));
  }
  if (!anyStance) stanceOffset = 0.16f;
  // A little slack is left in the leg (98% extension) so the IK never has to clamp: a
  // clamped leg is what makes a planted foot creep.
  const float legReachFull = legLen * 0.98f;
  const float requiredH = std::sqrt(std::fmax(legReachFull * legReachFull - stanceOffset * stanceOffset, 0.09f));
  const float hipAboveFeet = clampf(requiredH, legLen * 0.68f, legLen * 0.94f) + metrics_.footDrop;
  // Blend the foot-driven height with the simulated height so ledges/falls stay sane.
  const float simPelvisY = p.y + metrics_.hipHeight;
  // Grounded: the pelvis follows the feet (so the legs always reach). Airborne: it rides
  // the simulated jump. Blended by the smoothed airborne weight, so take-off and landing
  // never pop the body up or down.
  // Only the *support height* is damped: the set of support feet changes discretely (a
  // foot lands or lifts), and the hips must not jump when it does. The reach-derived part
  // (hipAboveFeet) is applied exactly — if it lagged, the leg would be over-extended and
  // the IK would pull the planted foot, which is exactly the creep the tests measure.
  if (!initialised_ || std::fabs(pelvisSupportY_) < 1e-6f) pelvisSupportY_ = footMidY;
  pelvisSupportY_ = lerp(pelvisSupportY_, footMidY, dampFactor(0.09f, step));
  const float groundedY = std::fmin(std::fmax(pelvisSupportY_ + hipAboveFeet, simPelvisY - 0.30f), simPelvisY + 0.15f);
  const float pelvisY = lerp(groundedY, p.y + hipH, airW_);
  const Vec3 pelvisPos{p.x + right2.x * leanSide_ * 0.05f, pelvisY, p.z + right2.y * leanSide_ * 0.05f};

  Mat4 pelvis = Mat4::translation(pelvisPos) * Mat4::rotationY(yaw) *
                Mat4::rotationZ(leanSide_ * 0.5f + 0.20f * kneelW_) *
                Mat4::rotationX(leanForward_ * 0.35f + 0.30f * crouchW_ + 0.45f * kneelW_);
  pose_.world(PJ_Pelvis) = pelvis;

  // --- spine ---------------------------------------------------------------------
  const float twist = 0.16f * moveW_ * std::sin(phase_);
  const float shiver = shiverW_ * 0.012f * std::sin(time_ * 34.0f);
  const float crouchFlex = 0.30f * crouchW_;
  const float gestureFlex = 0.55f * (gesturePose_[static_cast<int>(PlayerGesture::Gather)] +
                                     gesturePose_[static_cast<int>(PlayerGesture::Drink)] +
                                     gesturePose_[static_cast<int>(PlayerGesture::Eat)] +
                                     0.5f * gesturePose_[static_cast<int>(PlayerGesture::Fire)]);
  (void)gIdx;
  Mat4 spine1 = pelvis * Mat4::translation(skeleton_.bone(PJ_Spine1).bindOffset) *
                Mat4::rotationY(-twist * 0.4f) * Mat4::rotationX(0.20f * leanForward_ + crouchFlex * 0.35f + gestureFlex * 0.25f + shiver);
  pose_.world(PJ_Spine1) = spine1;
  Mat4 spine2 = spine1 * Mat4::translation(skeleton_.bone(PJ_Spine2).bindOffset) *
                Mat4::rotationY(-twist * 0.4f) * Mat4::rotationX(crouchFlex * 0.35f + gestureFlex * 0.35f + shiver * 1.2f);
  pose_.world(PJ_Spine2) = spine2;
  Mat4 chest = spine2 * Mat4::translation(skeleton_.bone(PJ_Chest).bindOffset) * Mat4::rotationY(-twist * 0.2f) *
               Mat4::rotationX(0.15f * leanForward_ + crouchFlex * 0.30f + gestureFlex * 0.40f) *
               Mat4::rotationZ(-leanSide_ * 0.4f);
  pose_.world(PJ_Chest) = chest;

  // --- look-at (PROMPT §10.9): smooth, clamped, camera- or interest-driven --------
  Vec3 lookDir = cameraForward;
  if (lengthSq(lookDir) < 1e-6f) lookDir = Vec3{fwd2.x, 0.0f, fwd2.y};
  lookDir = normalize(lookDir);
  // The head turns toward the camera direction, but only as far as the neck allows.
  // The relative angle is clamped *before* any wrap to (-pi, pi], so crossing behind the
  // player never snaps the head through its whole range (PROMPT §10.9: no unnatural
  // snapping) — the head just keeps looking as far sideways as it can.
  const float relYaw = std::atan2(lookDir.x, lookDir.z) - yaw;
  const float desiredYaw = clampf(relYaw, -1.05f, 1.05f);
  const float desiredPitch = clampf(-std::asin(clampf(lookDir.y, -1.0f, 1.0f)) * 0.55f, -0.30f, 0.22f);
  const float lookClamp = 1.0f - 0.65f * (moveW_ * runW_);  // don't stare while sprinting
  lookYaw_ = lerp(lookYaw_, desiredYaw * lookClamp, dampFactor(0.16f, step));
  lookPitch_ = lerp(lookPitch_, desiredPitch * lookClamp, dampFactor(0.20f, step));
  Mat4 neck = chest * Mat4::translation(skeleton_.bone(PJ_Neck).bindOffset) *
              Mat4::rotationY(lookYaw_ * 0.55f) * Mat4::rotationX(lookPitch_ * 0.5f + 0.22f * gestureFlex + shiver);
  pose_.world(PJ_Neck) = neck;
  Mat4 head = neck * Mat4::translation(skeleton_.bone(PJ_Head).bindOffset) * Mat4::rotationY(lookYaw_ * 0.45f) *
              Mat4::rotationX(lookPitch_ * 0.5f + 0.22f * gestureFlex + breathe * (1.0f - moveW_) * 0.01f + shiver);
  pose_.world(PJ_Head) = head;

  // --- arms: counter-rotating swing, IK-free procedural FK ------------------------
  for (int side = 0; side < 2; ++side) {
    const int clav = side == 0 ? PJ_ClavL : PJ_ClavR;
    const int upper = side == 0 ? PJ_UpperArmL : PJ_UpperArmR;
    const int lower = side == 0 ? PJ_LowerArmL : PJ_LowerArmR;
    const int hand = side == 0 ? PJ_HandL : PJ_HandR;
    const float s = side == 0 ? 1.0f : -1.0f;
    const float armPhase = phase_ + (side == 0 ? kPi : 0.0f);  // counter to the legs
    const float swing = (0.32f + 0.52f * runW_) * moveW_ * std::sin(armPhase);
    float shoulderPitch = swing;
    float shoulderRoll = 0.10f + 0.05f * breathe * (1.0f - moveW_) + 0.25f * airW_;
    float elbow = 0.28f + 1.00f * runW_ * moveW_ + 0.35f * crouchW_ + 0.45f * airW_;
    float upperTwist = 0.0f;
    // Idle: hands rest near the belt with a slow weight shift.
    shoulderPitch += 0.06f * std::sin(time_ * 0.7f + side * 1.7f) * (1.0f - moveW_);
    // Shivering arms tuck in.
    shoulderRoll += 0.10f * shiverW_;
    elbow += 0.35f * shiverW_;
    // Kneeling at the fire: forearms rest on the knees.
    shoulderPitch += 0.75f * kneelW_;
    elbow += 0.85f * kneelW_;
    // Gesture: the right hand reaches (left steadies). Each gesture's angles are
    // blended by its own weight, so a new interaction cross-fades over ~0.15 s.
    const float reach = side == 1 ? 1.0f : 0.35f;
    const float wGather = gesturePose_[static_cast<int>(PlayerGesture::Gather)] * reach;
    const float wDrink = gesturePose_[static_cast<int>(PlayerGesture::Drink)] * reach;
    const float wEat = gesturePose_[static_cast<int>(PlayerGesture::Eat)] * reach;
    const float wFire = gesturePose_[static_cast<int>(PlayerGesture::Fire)] * reach;
    const float wRest = gesturePose_[static_cast<int>(PlayerGesture::Rest)] * reach;
    shoulderPitch += 1.05f * wGather + 0.55f * wDrink + 0.62f * wEat + 0.85f * wFire + 0.20f * wRest;
    elbow += 0.30f * wGather + 1.45f * wDrink + 1.30f * wEat + 0.30f * wFire + 0.40f * wRest;
    upperTwist += 0.20f * wGather + 0.35f * wDrink + 0.30f * wEat + 0.15f * wFire;
    Mat4 clavM = chest * Mat4::translation(skeleton_.bone(clav).bindOffset) * Mat4::rotationX(0.05f * swing);
    pose_.world(clav) = clavM;
    Mat4 upperM = clavM * Mat4::translation(skeleton_.bone(upper).bindOffset) * Mat4::rotationZ(-shoulderRoll * s) *
                  Mat4::rotationY(upperTwist * s) * Mat4::rotationX(shoulderPitch);
    pose_.world(upper) = upperM;
    Mat4 lowerM = upperM * Mat4::translation(skeleton_.bone(lower).bindOffset) * Mat4::rotationX(-elbow);
    pose_.world(lower) = lowerM;
    pose_.world(hand) = lowerM * Mat4::translation(skeleton_.bone(hand).bindOffset) *
                        Mat4::rotationX(0.15f * std::sin(time_ * 1.3f + side) * (1.0f - moveW_));
  }

  // --- legs: two-bone IK to the foot targets (PROMPT §10.7) -----------------------
  for (int side = 0; side < 2; ++side) {
    const int thigh = side == 0 ? PJ_ThighL : PJ_ThighR;
    const int calf = side == 0 ? PJ_CalfL : PJ_CalfR;
    const int foot = side == 0 ? PJ_FootL : PJ_FootR;
    const int toe = side == 0 ? PJ_ToeL : PJ_ToeR;
    const Vec3 hip = pelvis.transformPoint(skeleton_.bone(thigh).bindOffset);
    const Vec3 target = footTarget[side] + Vec3{0.0f, metrics_.footDrop, 0.0f};
    // Knees bend forward in the sagittal plane (the lateral axis is the plane normal),
    // so a swinging leg can never flip its knee.
    const Vec3 lateral{-fwd2.y, 0.0f, fwd2.x};
    const IkResult ik = solveTwoBoneIkPlanar(hip, target, metrics_.thigh, metrics_.calf, lateral, -1.0f);
    pose_.world(thigh) = aimJointStable(hip, ik.jointPos, Vec3{fwd2.x, 0.0f, fwd2.y});
    pose_.world(calf) = aimJointStable(ik.jointPos, ik.endPos, Vec3{fwd2.x, 0.0f, fwd2.y});
    // Foot: the sole follows the terrain under it, damped so a terrain seam (or a foot
    // crossing a rock edge) does not snap the ankle — the foot rolls onto the new slope.
    const Vec3 sampled = terrainNormal(world, Vec2{ik.endPos.x, ik.endPos.z}, 0.5f);
    footNormal_[side] = normalize(lerp(footNormal_[side], sampled, dampFactor(0.10f, step)));
    const Vec3 n = footNormal_[side];
    const Vec3 anklePos = ik.endPos;
    const Vec3 soleDir = normalize(Vec3{n.x * 0.35f, -1.0f, n.z * 0.35f});
    pose_.world(foot) = aimJointStable(anklePos, anklePos + soleDir * 0.2f, Vec3{fwd2.x, 0.0f, fwd2.y});
    pose_.world(toe) = pose_.world(foot) * Mat4::translation(skeleton_.bone(toe).bindOffset) *
                       Mat4::rotationX(-0.15f * (1.0f - moveW_));
  }

  // Diagnostics: how far a planted foot moved this step (target: < 2 cm, §10.12).
  // Only movement while the foot stays planted counts as drift: the frame a foot touches
  // down legitimately moves it from the end of its swing arc onto the ground.
  if (skipDriftStep_) skipDriftStep_ = false;
  for (int side = 0; side < 2 && !skipDriftStep_; ++side) {
    const Vec3 world3 = pose_.world(side == 0 ? PJ_FootL : PJ_FootR).position();
    if (stance_[side] && wasStance_[side] && grounded && speed > 0.2f)
      maxFootDrift_ = std::fmax(maxFootDrift_, length(world3 - prevFootWorld_[side]));
    prevFootWorld_[side] = world3;
    wasStance_[side] = stance_[side];
  }
  pose_.computeSkin(skeleton_);
  // Guard against any non-finite pose (never shipped to the GPU).
  if (!pose_.finite()) {
    for (int i = 0; i < PJ_Count; ++i) pose_.world(i) = skeleton_.bindWorld(i);
    pose_.computeSkin(skeleton_);
  }
}

void PlayerRig::fillSkinPalette(float alpha, std::vector<Mat4>& out) const {
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
