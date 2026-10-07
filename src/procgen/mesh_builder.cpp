#include "procgen/mesh_builder.h"

#include <cmath>

namespace aaa::procgen {

uint32_t MeshData::addVertex(Vec3 p, Vec3 n, Vec2 uv, float ao, float wind, float flag, float phase) {
  vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y, toUnorm8(ao), toUnorm8(wind), toUnorm8(flag),
                      toUnorm8(phase)});
  return static_cast<uint32_t>(vertices.size() - 1);
}

void MeshData::append(const MeshData& o) {
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  vertices.insert(vertices.end(), o.vertices.begin(), o.vertices.end());
  for (uint32_t i : o.indices) indices.push_back(base + i);
}

void MeshData::recomputeNormals() {
  std::vector<Vec3> acc(vertices.size());
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const Vertex &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
    const Vec3 pa{a.px, a.py, a.pz}, pb{b.px, b.py, b.pz}, pc{c.px, c.py, c.pz};
    // Left-handed winding: clockwise front faces when viewed from outside.
    const Vec3 n = cross(pb - pa, pc - pa);
    for (int k = 0; k < 3; ++k) acc[indices[i + k]] += n;
  }
  for (size_t i = 0; i < vertices.size(); ++i) {
    const Vec3 n = normalize(acc[i]);
    vertices[i].nx = n.x; vertices[i].ny = n.y; vertices[i].nz = n.z;
  }
}

void MeshData::computeBounds() {
  boundsMin = {1e30f, 1e30f, 1e30f};
  boundsMax = {-1e30f, -1e30f, -1e30f};
  for (const Vertex& v : vertices) {
    boundsMin = {std::fmin(boundsMin.x, v.px), std::fmin(boundsMin.y, v.py), std::fmin(boundsMin.z, v.pz)};
    boundsMax = {std::fmax(boundsMax.x, v.px), std::fmax(boundsMax.y, v.py), std::fmax(boundsMax.z, v.pz)};
  }
}

void PropMesh::finalize() {
  opaque.computeBounds();
  foliage.computeBounds();
  boundsMin = {1e30f, 1e30f, 1e30f};
  boundsMax = {-1e30f, -1e30f, -1e30f};
  for (const MeshData* m : {&opaque, &foliage}) {
    if (m->vertices.empty()) continue;
    boundsMin = {std::fmin(boundsMin.x, m->boundsMin.x), std::fmin(boundsMin.y, m->boundsMin.y), std::fmin(boundsMin.z, m->boundsMin.z)};
    boundsMax = {std::fmax(boundsMax.x, m->boundsMax.x), std::fmax(boundsMax.y, m->boundsMax.y), std::fmax(boundsMax.z, m->boundsMax.z)};
  }
}

void addTube(MeshData& m, const std::vector<Vec3>& centre, const std::vector<float>& radii, int sides, float uvScaleV,
             float windBase, float windTip, float phase, bool capEnd) {
  const size_t rings = centre.size();
  if (rings < 2) return;
  const uint32_t base = static_cast<uint32_t>(m.vertices.size());
  float vAcc = 0.0f;
  Vec3 prevSide{1, 0, 0};
  for (size_t i = 0; i < rings; ++i) {
    const Vec3 t = normalize(i + 1 < rings ? centre[i + 1] - centre[i] : centre[i] - centre[i - 1]);
    // Parallel-transport-ish frame to avoid twisting.
    Vec3 side = normalize(prevSide - t * dot(prevSide, t));
    if (lengthSq(side) < 1e-6f) side = normalize(cross(t, std::fabs(t.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
    prevSide = side;
    const Vec3 bin = cross(t, side);
    if (i > 0) vAcc += length(centre[i] - centre[i - 1]);
    const float along = static_cast<float>(i) / static_cast<float>(rings - 1);
    const float wind = lerp(windBase, windTip, along);
    for (int s = 0; s <= sides; ++s) {
      const float a = kTwoPi * static_cast<float>(s) / static_cast<float>(sides);
      const Vec3 n = side * std::cos(a) + bin * std::sin(a);
      m.addVertex(centre[i] + n * radii[i], n, {static_cast<float>(s) / sides * 2.0f, vAcc * uvScaleV},
                  1.0f, wind, 0.0f, phase);
    }
  }
  const uint32_t stride = static_cast<uint32_t>(sides + 1);
  for (uint32_t i = 0; i + 1 < rings; ++i)
    for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) {
      const uint32_t a = base + i * stride + s, b = a + 1, c = a + stride + 1, d = a + stride;
      m.addQuad(a, b, c, d);  // front = cross(b-a, c-a) points outward
    }
  if (capEnd) {
    const Vec3 tip = centre.back();
    const Vec3 t = normalize(centre[rings - 1] - centre[rings - 2]);
    const uint32_t ci = m.addVertex(tip + t * radii.back() * 0.5f, t, {0.5f, vAcc * uvScaleV}, 1.0f, windTip, 0.0f, phase);
    const uint32_t ring = base + static_cast<uint32_t>(rings - 1) * stride;
    for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) m.addTriangle(ring + s, ring + s + 1, ci);
  }
}

}  // namespace aaa::procgen
