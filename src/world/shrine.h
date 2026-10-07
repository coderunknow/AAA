#pragma once
// The abandoned mountain shrine: a single-source description of the landmark's footprint.
// Both gameplay (walkable platform tops, colliders, interaction points) and the procedural
// mesh generator read this, so what you see is exactly what you collide with.
#include <vector>

#include "core/math.h"
#include "world/scatter.h"

namespace aaa {

class Heightfield;
struct WorldLayout;

// Box in shrine-local space (x = right, y = up, z = toward the entrance), rotated with the shrine.
struct ShrineBox {
  Vec3 center;  // local
  Vec3 half;
};

struct ShrineLayout {
  Vec2 center;
  float yaw = 0.0f;   // local +z (entrance) faces (sin yaw, cos yaw)
  float baseY = 0.0f; // terrace ground height at the centre
  float platformTop = 0.0f;

  std::vector<ShrineBox> walkable;  // platform + steps (tops are standable)
  std::vector<CircleCollider> colliders;
  Vec3 altar;       // interaction point (world)
  Vec3 restPoint;   // where the player wakes after resting / collapsing (world)
  Vec2 gateLeft, gateRight;  // gate post positions (world xz)

  Vec3 toWorld(Vec3 local) const;
  Vec2 toLocal(Vec2 world) const;
  // Highest standable surface at (x,z), or -inf when outside every walkable box.
  float surfaceHeight(float x, float z) const;
  // True inside the shrine's sanctuary radius (predators keep away, rest is possible).
  bool inSanctuary(Vec2 p) const { return length(p - center) < 14.0f; }
};

ShrineLayout makeShrineLayout(const WorldLayout& layout, const Heightfield& hf);

}  // namespace aaa
