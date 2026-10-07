#include "world/heightfield.h"

#include <cmath>

namespace aaa {

Heightfield::Heightfield(int resolution, float worldSize)
    : res_(resolution), size_(worldSize), cell_(worldSize / static_cast<float>(resolution - 1)),
      h_(static_cast<size_t>(resolution) * resolution, 0.0f) {}

bool Heightfield::contains(float x, float z) const {
  return std::fabs(x) <= halfSize() && std::fabs(z) <= halfSize();
}

float Heightfield::height(float x, float z) const {
  if (res_ == 0) return 0.0f;
  const float fx = clampf((x + halfSize()) / cell_, 0.0f, static_cast<float>(res_ - 1) - 0.001f);
  const float fz = clampf((z + halfSize()) / cell_, 0.0f, static_cast<float>(res_ - 1) - 0.001f);
  const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
  const float tx = fx - ix, tz = fz - iz;
  const float h00 = at(ix, iz), h10 = at(ix + 1, iz), h01 = at(ix, iz + 1), h11 = at(ix + 1, iz + 1);
  return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);
}

Vec3 Heightfield::normal(float x, float z) const {
  const float e = cell_;
  const float hl = height(x - e, z), hr = height(x + e, z);
  const float hd = height(x, z - e), hu = height(x, z + e);
  return normalize(Vec3{hl - hr, 2.0f * e, hd - hu});
}

bool Heightfield::raycast(Vec3 origin, Vec3 dir, float maxDist, float& outDist) const {
  const float step = cell_ * 0.5f;
  float prevT = 0.0f;
  float prevDiff = origin.y - height(origin.x, origin.z);
  if (prevDiff < 0.0f) { outDist = 0.0f; return true; }
  for (float t = step; t <= maxDist + step * 0.5f; t += step) {
    const float tt = t > maxDist ? maxDist : t;
    const Vec3 p = origin + dir * tt;
    const float diff = p.y - height(p.x, p.z);
    if (diff < 0.0f) {
      // Linear refinement between the last two samples.
      outDist = prevT + (tt - prevT) * (prevDiff / (prevDiff - diff));
      return true;
    }
    prevT = tt;
    prevDiff = diff;
  }
  return false;
}

void Heightfield::updateBounds() {
  if (h_.empty()) return;
  minH_ = maxH_ = h_[0];
  for (float v : h_) {
    minH_ = v < minH_ ? v : minH_;
    maxH_ = v > maxH_ ? v : maxH_;
  }
}

}  // namespace aaa
