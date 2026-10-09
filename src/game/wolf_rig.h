#pragma once
// Procedurally generated skinned wolf (PROMPT §10.10): 26 joints (spine chain, neck,
// head, jaw, ears, tail chain, four three-segment legs), a smooth skinned body mesh
// with procedural fur cues, and procedural locomotion: walk / trot / lope gait driven
// by speed, spine flex, ear spring, tail spring chain, crouch-stalk and flee postures,
// with planted feet (two-bone IK) during stance.
#include <vector>

#include "core/math.h"
#include "game/skin.h"

namespace aaa {

class World;
struct Wolf;
struct WildlifeContext;

// Joint indices (26 joints, PROMPT §10.1).
enum WolfJoint : int {
  WJ_Pelvis = 0, WJ_Spine1, WJ_Spine2, WJ_Chest, WJ_Neck1, WJ_Neck2, WJ_Head, WJ_Jaw,
  WJ_EarL, WJ_EarR,
  WJ_Tail1, WJ_Tail2, WJ_Tail3, WJ_Tail4,
  WJ_FrontUpperL, WJ_FrontLowerL, WJ_FrontPawL,
  WJ_FrontUpperR, WJ_FrontLowerR, WJ_FrontPawR,
  WJ_HindUpperL, WJ_HindLowerL, WJ_HindPawL,
  WJ_HindUpperR, WJ_HindLowerR, WJ_HindPawR,
  WJ_Count
};

class WolfRig {
 public:
  void build(int detail);
  bool built() const { return skeleton_.count() > 0; }
  const Skeleton& skeleton() const { return skeleton_; }
  const SkinnedMesh& mesh() const { return mesh_; }

  void reset(const Wolf& wolf, const World& world);
  // `dt` advances the animator; `alert` (0..1) tightens the stance when stalking.
  void update(float dt, const Wolf& wolf, const World& world);
  const Pose& pose() const { return pose_; }
  void fillSkinPalette(float alpha, std::vector<Mat4>& out) const;

  float maxFootDrift() const { return maxFootDrift_; }

 private:
  void buildMesh(int detail);
  float groundAt(const World& world, Vec2 xz) const;

  Skeleton skeleton_;
  SkinnedMesh mesh_;
  Pose pose_;
  Pose prev_;
  float phase_ = 0.0f;
  float distance_ = 0.0f;
  float gaitW_ = 0.0f;
  float lopeW_ = 0.0f;
  float stalkW_ = 0.0f;
  float fleeW_ = 0.0f;
  float time_ = 0.0f;
  float tailSpring_[3] = {0.0f, 0.0f, 0.0f};
  float earSpring_[2] = {0.0f, 0.0f};
  float prevSpeed_ = 0.0f;
  float maxFootDrift_ = 0.0f;
  bool initialised_ = false;
  bool skipDriftStep_ = false;  // the first step after reset() only settles the pose
  bool wasStance_[4] = {true, true, false, false};  // previous step's stance (drift metric)
  Vec3 plant_[4];
  Vec3 lift_[4];
  Vec3 nextPlant_[4];
  bool stance_[4] = {true, true, false, false};
  Vec3 prevFootWorld_[4];
};

}  // namespace aaa
