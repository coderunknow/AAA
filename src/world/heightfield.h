#pragma once
// Regular-grid heightfield covering a square world centred on the origin.
#include <cstdint>
#include <vector>

#include "core/math.h"

namespace aaa {

class Heightfield {
 public:
  Heightfield() = default;
  Heightfield(int resolution, float worldSize);

  int resolution() const { return res_; }            // samples per side (e.g. 1025)
  float worldSize() const { return size_; }          // metres per side
  float cellSize() const { return cell_; }
  float halfSize() const { return size_ * 0.5f; }
  float minHeight() const { return minH_; }
  float maxHeight() const { return maxH_; }

  float& at(int ix, int iz) { return h_[static_cast<size_t>(iz) * res_ + ix]; }
  float at(int ix, int iz) const { return h_[static_cast<size_t>(iz) * res_ + ix]; }
  const std::vector<float>& data() const { return h_; }

  // World-space sample position of a grid index.
  float worldX(int ix) const { return -halfSize() + ix * cell_; }
  float worldZ(int iz) const { return -halfSize() + iz * cell_; }

  bool contains(float x, float z) const;
  float height(float x, float z) const;  // bilinear, clamped at the border
  Vec3 normal(float x, float z) const;   // central differences
  float slope(float x, float z) const { return 1.0f - normal(x, z).y; }  // 0 = flat

  // Ray march against the heightfield. Returns true and the hit distance if the
  // segment [origin, origin + dir * maxDist] intersects the terrain.
  bool raycast(Vec3 origin, Vec3 dir, float maxDist, float& outDist) const;

  void updateBounds();

 private:
  int res_ = 0;
  float size_ = 0, cell_ = 1;
  float minH_ = 0, maxH_ = 0;
  std::vector<float> h_;
};

}  // namespace aaa
