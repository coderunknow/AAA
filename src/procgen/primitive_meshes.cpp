#include "procgen/primitive_meshes.h"

#include <cmath>

namespace aaa::procgen {

MeshData makeCapsule(int sides, int capRings) {
  // Profile from top hemisphere (y=0) down to bottom hemisphere (y=-1); radius 1
  // scaled non-uniformly by the caller, so hemispheres are squashed into the length.
  MeshData m;
  std::vector<std::pair<float, float>> profile;  // (y, r)
  const float capH = 0.12f;
  for (int i = 0; i <= capRings; ++i) {
    const float a = kPi * 0.5f * (1.0f - static_cast<float>(i) / capRings);
    profile.push_back({capH * std::sin(a) - capH, std::cos(a)});
  }
  for (int i = 0; i <= capRings; ++i) {
    const float a = kPi * 0.5f * static_cast<float>(i) / capRings;
    profile.push_back({-1.0f + capH - capH * std::sin(a), std::cos(a)});
  }
  const uint32_t stride = static_cast<uint32_t>(sides + 1);
  for (size_t r = 0; r < profile.size(); ++r)
    for (int s = 0; s <= sides; ++s) {
      const float a = kTwoPi * s / sides;
      const Vec3 p{std::cos(a) * profile[r].second, profile[r].first, std::sin(a) * profile[r].second};
      m.addVertex(p, normalize(Vec3{p.x, 0.0f, p.z}), {static_cast<float>(s) / sides, static_cast<float>(r) / profile.size()});
    }
  for (uint32_t r = 0; r + 1 < profile.size(); ++r)
    for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) {
      const uint32_t a = r * stride + s;
      m.addQuad(a, a + 1, a + stride + 1, a + stride);
    }
  m.recomputeNormals();
  return m;
}

MeshData makeBox() {
  MeshData m;
  const Vec3 faces[6][3] = {  // normal, u axis, v axis
      {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
      {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}};
  for (const auto& f : faces) {
    const Vec3 n = f[0], u = f[1], v = f[2];
    uint32_t idx[4];
    const float cs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 4; ++i) {
      const Vec3 p = n + u * cs[i][0] + v * cs[i][1];
      // Bevelled normal: lean toward the corner for softer edges.
      idx[i] = m.addVertex(p, normalize(n * 1.6f + p * 0.4f), {cs[i][0] * 0.5f + 0.5f, cs[i][1] * 0.5f + 0.5f});
    }
    m.addQuad(idx[0], idx[3], idx[2], idx[1]);
  }
  return m;
}

MeshData makeSphere(int sides, int rings) {
  MeshData m;
  const uint32_t stride = static_cast<uint32_t>(sides + 1);
  for (int r = 0; r <= rings; ++r) {
    const float phi = kPi * r / rings;
    for (int s = 0; s <= sides; ++s) {
      const float th = kTwoPi * s / sides;
      const Vec3 n{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
      m.addVertex(n, n, {static_cast<float>(s) / sides, static_cast<float>(r) / rings});
    }
  }
  for (uint32_t r = 0; r < static_cast<uint32_t>(rings); ++r)
    for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) {
      const uint32_t a = r * stride + s;
      m.addQuad(a, a + 1, a + stride + 1, a + stride);
    }
  return m;
}

MeshData makeCone(int sides) {
  // Conical straw hat: shallow cone + brim lip.
  MeshData m;
  const uint32_t apex = m.addVertex({0, 1.0f, 0}, {0, 1, 0}, {0.5f, 0.5f});
  const uint32_t first = static_cast<uint32_t>(m.vertices.size());
  for (int s = 0; s <= sides; ++s) {
    const float a = kTwoPi * s / sides;
    const Vec3 p{std::cos(a), -0.3f, std::sin(a)};
    m.addVertex(p, normalize(Vec3{p.x * 0.6f, 1.0f, p.z * 0.6f}), {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)});
  }
  for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) m.addTriangle(apex, first + s + 1, first + s);
  // Underside so the hat is visible from below.
  const uint32_t under = m.addVertex({0, 0.2f, 0}, {0, -1, 0}, {0.5f, 0.5f}, 0.6f);
  const uint32_t ring2 = static_cast<uint32_t>(m.vertices.size());
  for (int s = 0; s <= sides; ++s) {
    const float a = kTwoPi * s / sides;
    m.addVertex({std::cos(a), -0.3f, std::sin(a)}, {0, -1, 0}, {0.5f, 0.5f}, 0.6f);
  }
  for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) m.addTriangle(under, ring2 + s, ring2 + s + 1);
  return m;
}

}  // namespace aaa::procgen
