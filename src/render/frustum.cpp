#include "render/frustum.h"

#include <cmath>

namespace aaa {

void Frustum::fromViewProj(const float* m) {
  auto col = [&](int j) { return Vec4{m[j], m[4 + j], m[8 + j], m[12 + j]}; };
  const Vec4 c0 = col(0), c1 = col(1), c2 = col(2), c3 = col(3);
  auto add = [](Vec4 a, Vec4 b, float s) { return Vec4{a.x + s * b.x, a.y + s * b.y, a.z + s * b.z, a.w + s * b.w}; };
  planes[0] = add(c3, c0, 1.0f);
  planes[1] = add(c3, c0, -1.0f);
  planes[2] = add(c3, c1, 1.0f);
  planes[3] = add(c3, c1, -1.0f);
  planes[4] = add(c3, c2, -1.0f);
  for (Vec4& p : planes) {
    const float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    p = {p.x / l, p.y / l, p.z / l, p.w / l};
  }
}

bool Frustum::sphereVisible(Vec3 c, float r) const {
  for (const Vec4& p : planes)
    if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
  return true;
}

bool Frustum::aabbVisible(Vec3 mn, Vec3 mx) const {
  for (const Vec4& p : planes) {
    const Vec3 v{p.x >= 0 ? mx.x : mn.x, p.y >= 0 ? mx.y : mn.y, p.z >= 0 ? mx.z : mn.z};
    if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
  }
  return true;
}

}  // namespace aaa
