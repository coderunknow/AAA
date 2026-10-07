#include "world/shrine.h"

#include <cmath>
#include <limits>

#include "world/heightfield.h"
#include "world/world_layout.h"

namespace aaa {

Vec3 ShrineLayout::toWorld(Vec3 l) const {
  const float s = std::sin(yaw), c = std::cos(yaw);
  // right = (c, -s), forward = (s, c)
  return {center.x + l.x * c + l.z * s, l.y, center.y - l.x * s + l.z * c};
}

Vec2 ShrineLayout::toLocal(Vec2 w) const {
  const float s = std::sin(yaw), c = std::cos(yaw);
  const Vec2 d = w - center;
  return {d.x * c - d.y * s, d.x * s + d.y * c};
}

float ShrineLayout::surfaceHeight(float x, float z) const {
  const Vec2 l = toLocal({x, z});
  float best = -std::numeric_limits<float>::infinity();
  for (const ShrineBox& b : walkable) {
    if (std::fabs(l.x - b.center.x) <= b.half.x && std::fabs(l.y - b.center.z) <= b.half.z)
      best = std::fmax(best, b.center.y + b.half.y);
  }
  return best;
}

ShrineLayout makeShrineLayout(const WorldLayout& L, const Heightfield& hf) {
  ShrineLayout s;
  s.center = L.shrine;
  // Face the end of the trail so the player arrives at the gate and looks up the steps.
  const Vec2 arrival = L.trail.points.size() > 8 ? L.trail.points[L.trail.points.size() - 8] : L.spawn;
  const Vec2 toArrival = arrival - L.shrine;
  s.yaw = std::atan2(toArrival.x, toArrival.y);
  s.baseY = hf.height(L.shrine.x, L.shrine.y);
  const float top = s.baseY + 0.55f;
  s.platformTop = top;

  // Platform sinks 0.9 m into the terrace so slopes never reveal its underside.
  s.walkable.push_back({{0.0f, top - 0.725f, 0.0f}, {5.2f, 0.725f, 4.2f}});
  s.walkable.push_back({{0.0f, s.baseY + 0.18f - 0.5f, 4.75f}, {1.9f, 0.5f, 0.55f}});   // lower step
  s.walkable.push_back({{0.0f, s.baseY + 0.37f - 0.5f, 4.45f}, {1.9f, 0.5f, 0.25f}});   // upper step

  auto addCol = [&](float lx, float lz, float r, float topY) {
    const Vec3 w = s.toWorld({lx, 0.0f, lz});
    s.colliders.push_back({{w.x, w.z}, r, topY});
  };
  // Pillars (front-right one is broken but still blocks).
  const float px = 3.6f, pz = 2.8f;
  for (float x : {-px, px})
    for (float z : {-pz, pz}) addCol(x, z, 0.24f, top + 3.0f);
  addCol(-1.2f, -pz, 0.24f, top + 3.0f);
  addCol(1.2f, -pz, 0.24f, top + 3.0f);
  // Low back wall.
  for (float x = -3.3f; x <= 3.31f; x += 0.66f) addCol(x, -3.0f, 0.36f, top + 1.1f);
  // Altar.
  addCol(0.0f, -2.0f, 0.7f, top + 0.9f);
  // Stone lanterns in front of the steps.
  for (float x : {-2.9f, 2.9f}) {
    const Vec3 w = s.toWorld({x, 0.0f, 6.4f});
    s.colliders.push_back({{w.x, w.z}, 0.38f, hf.height(w.x, w.z) + 1.9f});
  }
  // Gate further down the approach.
  for (float x : {-2.0f, 2.0f}) {
    const Vec3 w = s.toWorld({x, 0.0f, 12.5f});
    s.colliders.push_back({{w.x, w.z}, 0.26f, hf.height(w.x, w.z) + 3.8f});
    (x < 0 ? s.gateLeft : s.gateRight) = {w.x, w.z};
  }

  s.altar = s.toWorld({0.0f, top + 0.9f, -2.0f});
  s.restPoint = s.toWorld({0.0f, top, 0.6f});
  s.restPoint.y = top;
  return s;
}

}  // namespace aaa
