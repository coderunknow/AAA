#pragma once
#include "core/math.h"

namespace aaa {

// View frustum from a bx-convention view-projection matrix (row-vector: clip = v * M).
struct Frustum {
  Vec4 planes[5];  // left, right, bottom, top, far (near is implied by the side planes)
  void fromViewProj(const float* m);
  bool sphereVisible(Vec3 c, float r) const;
  bool aabbVisible(Vec3 mn, Vec3 mx) const;
};

}  // namespace aaa
