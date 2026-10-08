#pragma once
// Procedural skinned-mesh core (PROMPT §10.1–§10.2, §10.7, §10.12).
//
// Renderer-independent: skeletons, poses, skinned geometry, distance-based skin
// weights and an analytic two-bone IK solver. Everything is generated in code from
// a small declarative description, so meshes and rigs carry no third-party assets.
//
// Conventions: Y up, left-handed, metres, column-major Mat4 (see core/math.h).
// Joint world matrices are pure rotation+translation (no scale), which makes the
// bind-pose inverse exact (Mat4::inverseRigid) and keeps normals well behaved.
#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace aaa {

// ---------------------------------------------------------------------------
// Skeleton
// ---------------------------------------------------------------------------
struct Bone {
  std::string name;
  int parent = -1;         // -1 = root
  Vec3 bindOffset;         // position in the parent's bind space
};

class Skeleton {
 public:
  int addBone(const char* name, int parent, Vec3 bindOffset);
  int index(const char* name) const;  // -1 when absent
  // Computes bindWorld / inverseBind. Must be called before use.
  void finalize();

  int count() const { return static_cast<int>(bones_.size()); }
  const Bone& bone(int i) const { return bones_[i]; }
  const std::vector<Bone>& bones() const { return bones_; }
  const Mat4& bindWorld(int i) const { return bindWorld_[i]; }
  const Mat4& inverseBind(int i) const { return inverseBind_[i]; }
  // Bind-pose bone endpoints (root -> tip), useful for weight assignment and tests.
  Vec3 bindTip(int i) const;

 private:
  std::vector<Bone> bones_;
  std::vector<Mat4> bindWorld_;
  std::vector<Mat4> inverseBind_;
};

// ---------------------------------------------------------------------------
// Pose: world-space joint matrices + derived skinning palette.
// ---------------------------------------------------------------------------
class Pose {
 public:
  void resize(int joints);
  int count() const { return static_cast<int>(world_.size()); }
  Mat4& world(int i) { return world_[i]; }
  const Mat4& world(int i) const { return world_[i]; }
  const std::vector<Mat4>& worlds() const { return world_; }
  // skin[i] = world[i] * inverseBind[i] — the per-joint skinning matrix.
  const std::vector<Mat4>& skin() const { return skin_; }
  void computeSkin(const Skeleton& skeleton);
  // Blends `a` (weight 1-t) with `b` (weight t) into this pose and recomputes skin
  // matrices. Used for render-time interpolation and state cross-fades.
  void blend(const Pose& a, const Pose& b, float t, const Skeleton& skeleton);
  // Naive blend of all joint matrices (no skeleton needed) — for cross-fading two
  // already-computed poses.
  void blendInto(const Pose& other, float t);
  // True when every matrix element is finite.
  bool finite() const;

 private:
  std::vector<Mat4> world_;
  std::vector<Mat4> skin_;
};

// ---------------------------------------------------------------------------
// Skinned geometry
// ---------------------------------------------------------------------------
// 48-byte vertex: position, normal, uv, 4 joints + 4 normalized 8-bit weights, and
// a per-vertex material record (albedo, roughness, weave, emissive) so one draw
// call renders the whole body with its clothing.
struct SkinVertex {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
  uint8_t joints[4];
  uint8_t weights[4];        // sum to 255
  uint8_t r, g, b;           // linear albedo (unorm8)
  uint8_t rough;             // unorm8
  uint8_t weaveFreq;         // detail-texture frequency / 2
  uint8_t weaveStrength;     // unorm8
  uint8_t emissive;          // unorm8, x8
  uint8_t pad = 0;
};
static_assert(sizeof(SkinVertex) == 48, "skin vertex layout must stay tightly packed");

struct SkinMaterial {
  Vec3 albedo{0.5f, 0.5f, 0.5f};
  float roughness = 0.8f;
  float weaveFreq = 0.0f;
  float weaveStrength = 0.0f;
  float emissive = 0.0f;
  static SkinMaterial make(Vec3 albedo, float roughness, float weaveFreq = 0.0f, float weaveStrength = 0.0f,
                           float emissive = 0.0f) {
    return SkinMaterial{albedo, roughness, weaveFreq, weaveStrength, emissive};
  }
};

struct SkinnedMesh {
  std::vector<SkinVertex> vertices;
  std::vector<uint32_t> indices;
  Vec3 boundsMin{1e30f, 1e30f, 1e30f};
  Vec3 boundsMax{-1e30f, -1e30f, -1e30f};
  size_t triangleCount() const { return indices.size() / 3; }
  bool empty() const { return indices.empty(); }
  void computeBounds();
};

// Weight assignment: the four closest bone segments share the vertex, weighted by
// 1/(d^falloff). Bones given in `rigid` receive the full weight (hat, backpack, ...).
void assignSkinWeights(const Skeleton& skeleton, Vec3 position, const std::vector<int>* rigid, uint8_t joints[4],
                       uint8_t weights[4]);

// Sweeps a smooth tapered tube along a centreline and appends it to `mesh`
// (centre/radii are in bind world space; weights come from assignSkinWeights unless
// `rigidBone` >= 0). `wind`/`phase` are unused for characters but keep the parameter
// list uniform; normals come from the surface.
void addSkinSweep(SkinnedMesh& mesh, const Skeleton& skeleton, const std::vector<Vec3>& centre,
                  const std::vector<float>& radii, int sides, const SkinMaterial& material,
                  bool capStart, bool capEnd, int rigidBone = -1, const std::vector<int>* rigid = nullptr);

// Ellipsoid (used for heads, chests, joints): smooth, skinned.
void addSkinEllipsoid(SkinnedMesh& mesh, const Skeleton& skeleton, Vec3 centre, Vec3 radii, int segments, int rings,
                      const SkinMaterial& material, int rigidBone = -1);
// Axis-aligned box (rigid attachments such as the backpack).
void addSkinBox(SkinnedMesh& mesh, const Skeleton& skeleton, const Mat4& transform, Vec3 halfExtents,
                const SkinMaterial& material, int rigidBone);
// Flat cone ring (straw hat brim).
void addSkinCone(SkinnedMesh& mesh, const Skeleton& skeleton, const Mat4& transform, float radius, float height,
                 int segments, const SkinMaterial& material, int rigidBone);

// ---------------------------------------------------------------------------
// Two-bone IK (PROMPT §10.7)
// ---------------------------------------------------------------------------
struct IkResult {
  Vec3 jointPos;    // solved middle joint (knee/elbow) position
  Vec3 endPos;      // the reachable end position (clamped to the chain's reach)
  bool reachable = true;
};
// Solves a two-bone chain: root -> joint -> end, with the middle joint bending in the
// plane defined by (`pole` direction) and aiming to put `end` at `target`.
// Segment lengths are respected; when the target is out of reach the end is pulled
// toward the root and `reachable` is false (no NaN, no snapping).
IkResult solveTwoBoneIk(Vec3 root, Vec3 target, float upperLen, float lowerLen, Vec3 poleDir);

// Solves a two-bone chain that bends in a *fixed* plane, which is how real limbs work
// (knees bend in the sagittal plane). `bendAxis` is the plane's normal — the character's
// lateral axis — and `bendSign` selects which way the middle joint points (+1 / -1).
// Unlike the pole-projection solver this never degenerates when the limb passes through
// the pole direction, so a swinging leg cannot flip its knee mid-stride.
IkResult solveTwoBoneIkPlanar(Vec3 root, Vec3 target, float upperLen, float lowerLen, Vec3 bendAxis, float bendSign);

// Builds a joint world matrix that maps the bind direction (-Y by convention) onto
// (to - from) with `roll` radians about that axis. The remaining roll is chosen from a
// fixed reference axis, which is unstable when the direction passes vertical — use
// aimJointStable when the joint's twist is visible (legs, paws, feet).
Mat4 aimJoint(Vec3 from, Vec3 to, float roll);

// Same, but with the joint's forward axis derived from `refForward` (typically the
// character's facing): the frame stays continuous through vertical directions, so no
// joint ever flips its twist mid-animation (PROMPT §10.11, no visible pose popping).
Mat4 aimJointStable(Vec3 from, Vec3 to, Vec3 refForward, float roll = 0.0f);

}  // namespace aaa
