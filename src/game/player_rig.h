#pragma once
// Procedurally generated skinned survivor (PROMPT §10.1–§10.3, §10.6–§10.9).
//
// Builds a 22-joint skeleton and a smooth skinned body + layered clothing mesh in
// code, then animates it procedurally: speed-driven gait, two-bone foot IK planted on
// the terrain, pelvis bob/sway, counter-rotating arm swing, acceleration/turn lean,
// blended states (idle, crouch, air, landing, wading, gather, drink, fire, rest,
// shivering) and a smoothed look-at head. Renderer-independent and unit-testable.
#include <vector>

#include "core/math.h"
#include "game/game.h"
#include "game/skin.h"

namespace aaa {

class PlayerController;
class World;

// Joint indices (22 joints, PROMPT §10.1).
enum PlayerJoint : int {
  PJ_Pelvis = 0, PJ_Spine1, PJ_Spine2, PJ_Chest, PJ_Neck, PJ_Head,
  PJ_ClavL, PJ_UpperArmL, PJ_LowerArmL, PJ_HandL,
  PJ_ClavR, PJ_UpperArmR, PJ_LowerArmR, PJ_HandR,
  PJ_ThighL, PJ_CalfL, PJ_FootL, PJ_ToeL,
  PJ_ThighR, PJ_CalfR, PJ_FootR, PJ_ToeR,
  PJ_Count
};

// Arm/leg bind lengths (metres) — shared with the animator and the tests.
struct PlayerRigMetrics {
  float hipHeight = 0.94f;
  float thigh = 0.45f;
  float calf = 0.44f;
  float footDrop = 0.075f;   // ankle -> sole
  float stride = 0.78f;      // base stride at a walk
};

class PlayerRig {
 public:
  // Builds skeleton + mesh. `detail` 0..2 selects tessellation (low/medium/high).
  void build(int detail);

  const Skeleton& skeleton() const { return skeleton_; }
  const SkinnedMesh& mesh() const { return mesh_; }
  const PlayerRigMetrics& metrics() const { return metrics_; }

  // Re-seeds the gait state (spawn / teleport): plants the feet where the player is.
  void reset(const PlayerController& player, const World& world);

  // Produces this simulation step's pose (world-space joint matrices).
  void update(float dt, const PlayerController& player, const World& world, Vec3 cameraForward, float cold,
              float fireHeat, PlayerGesture gesture, float gestureAge);

  const Pose& pose() const { return pose_; }
  // Render-time interpolation: blends the previous and current skinning palettes
  // (fixed-step simulation at 60 Hz, PROMPT §8.6).
  void fillSkinPalette(float alpha, std::vector<Mat4>& out) const;

  // Diagnostics for the animation tests.
  const Vec3& footPlant(int i) const { return plant_[i]; }
  Vec3 footPosition(int i) const;
  float maxFootDrift() const { return maxFootDrift_; }
  float gaitPhase() const { return phase_; }
  bool footIsStance(int i) const { return stance_[i]; }

 private:
  void buildMesh(int detail);
  // Samples the terrain under a world XZ position (keeps the feet on the ground).
  float groundAt(const World& world, Vec2 xz) const;

  Skeleton skeleton_;
  SkinnedMesh mesh_;
  PlayerRigMetrics metrics_;
  Pose pose_;
  Pose prev_;

  // Gait / state.
  float phase_ = 0.0f;
  float lastDistance_ = 0.0f;
  float moveW_ = 0.0f;
  float runW_ = 0.0f;
  float crouchW_ = 0.0f;
  float airW_ = 0.0f;
  float wadeW_ = 0.0f;
  float gestureW_ = 0.0f;
  // Per-gesture pose weights, each damped separately so switching gesture mid-reach
  // cross-fades instead of popping (PROMPT §10.11).
  float gesturePose_[static_cast<int>(PlayerGesture::Rest) + 1] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  float shiverW_ = 0.0f;
  float kneelW_ = 0.0f;
  float time_ = 0.0f;
  float leanForward_ = 0.0f;
  float leanSide_ = 0.0f;
  float prevSpeed_ = 0.0f;
  float lookYaw_ = 0.0f;
  float lookPitch_ = 0.0f;
  float pelvisSupportY_ = 0.0f;  // damped grounded pelvis height (support-foot driven)
  float maxFootDrift_ = 0.0f;
  bool initialised_ = false;
  bool skipDriftStep_ = false;  // the first step after reset() only settles the pose
  bool wasStance_[2] = {true, true};  // previous step's stance state (drift metric)

  // Per-foot plant state: the world position the foot was planted at (no sliding
  // while in stance) and the last lift-off position.
  Vec3 plant_[2];   // where the foot is currently planted (no sliding while in stance)
  Vec3 lift_[2];    // where the swing started
  Vec3 nextPlant_[2];  // where the current swing will land
  Vec3 airFoot_[2];    // damped foot position while airborne (push-off -> tuck -> reach)
  Vec3 footNormal_[2] = {{0, 1, 0}, {0, 1, 0}};  // damped terrain normal under each foot
  bool stance_[2] = {true, true};
  Vec3 prevFootWorld_[2];
};

}  // namespace aaa
