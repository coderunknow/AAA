#include "procgen/rock_meshes.h"

#include <cmath>
#include <map>

#include "core/noise.h"
#include "core/rng.h"

namespace aaa::procgen {
namespace {

MeshData icosphere(int subdiv) {
  MeshData m;
  const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
  std::vector<Vec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                         {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  std::vector<uint32_t> f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                             3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
  for (auto& p : v) p = normalize(p);
  for (int s = 0; s < subdiv; ++s) {
    std::map<uint64_t, uint32_t> mid;
    auto midpoint = [&](uint32_t a, uint32_t b) {
      const uint64_t key = a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a);
      auto it = mid.find(key);
      if (it != mid.end()) return it->second;
      v.push_back(normalize(v[a] + v[b]));
      return mid[key] = static_cast<uint32_t>(v.size() - 1);
    };
    std::vector<uint32_t> nf;
    for (size_t i = 0; i < f.size(); i += 3) {
      const uint32_t a = f[i], b = f[i + 1], c = f[i + 2];
      const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
      nf.insert(nf.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
    }
    f.swap(nf);
  }
  for (const Vec3& p : v) m.addVertex(p, p, {0.5f + std::atan2(p.z, p.x) / kTwoPi, 0.5f - std::asin(p.y) / kPi});
  // Icosahedron faces above are counter-clockwise seen from outside in a right-handed
  // sense; flip to match our convention (front = cross(b-a, c-a) outward).
  for (size_t i = 0; i < f.size(); i += 3) m.addTriangle(f[i], f[i + 2], f[i + 1]);
  return m;
}

void fixWinding(MeshData& m) {
  // Ensure faces point away from the centroid (robust after heavy displacement).
  Vec3 c{};
  for (auto& v : m.vertices) c += Vec3{v.px, v.py, v.pz};
  c = c / static_cast<float>(m.vertices.size());
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    const auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &d = m.vertices[m.indices[i + 2]];
    const Vec3 pa{a.px, a.py, a.pz}, pb{b.px, b.py, b.pz}, pd{d.px, d.py, d.pz};
    if (dot(cross(pb - pa, pd - pa), (pa + pb + pd) / 3.0f - c) < 0) std::swap(m.indices[i + 1], m.indices[i + 2]);
  }
}

void bakeCavityAO(MeshData& m, uint32_t seed, float scale) {
  // Cheap AO: crevices (concave noise) and the underside are darker.
  for (auto& v : m.vertices) {
    const float n = noise::fbm3(v.px * scale, v.py * scale, v.pz * scale, seed + 5, 3);
    const float under = smoothstep(-0.6f, 0.3f, v.ny);
    v.r = toUnorm8(clampf(0.55f + 0.45f * under + 0.25f * n, 0.25f, 1.0f));
  }
}

}  // namespace

PropMesh makeRock(uint32_t seed, int lod) {
  Rng rng(seed * 2741u + 7u);
  PropMesh out;
  MeshData m = icosphere(lod == 0 ? 3 : (lod == 1 ? 2 : 1));
  const Vec3 sc{rng.range(0.8f, 1.3f), rng.range(0.45f, 0.75f), rng.range(0.7f, 1.1f)};
  for (auto& v : m.vertices) {
    const Vec3 p{v.px, v.py, v.pz};
    float d = 1.0f + 0.32f * noise::fbm3(p.x * 1.3f, p.y * 1.3f, p.z * 1.3f, seed, 4);
    // Planar facets: quantise along a few random planes for a fractured look.
    d -= 0.08f * std::fabs(noise::gradient3(p.x * 3.0f, p.y * 3.0f, p.z * 3.0f, seed + 9));
    Vec3 q = p * d * sc;
    if (q.y < -0.15f) q.y = -0.15f + (q.y + 0.15f) * 0.3f;  // flattened base sits on the ground
    v.px = q.x; v.py = q.y + 0.15f; v.pz = q.z;
  }
  fixWinding(m);
  m.recomputeNormals();
  bakeCavityAO(m, seed, 2.5f);
  out.opaque = std::move(m);
  out.finalize();
  return out;
}

PropMesh makeBoulder(uint32_t seed, int lod) {
  Rng rng(seed * 5167u + 3u);
  PropMesh out;
  MeshData m = icosphere(lod == 0 ? 4 : (lod == 1 ? 3 : 2));
  const Vec3 sc{rng.range(1.6f, 2.3f), rng.range(1.1f, 1.7f), rng.range(1.4f, 2.0f)};
  const float strataFreq = rng.range(5.0f, 8.0f);
  for (auto& v : m.vertices) {
    const Vec3 p{v.px, v.py, v.pz};
    float d = 1.0f + 0.28f * noise::fbm3(p.x * 1.1f, p.y * 1.1f, p.z * 1.1f, seed, 5);
    d += 0.035f * std::sin(p.y * strataFreq * kPi + noise::gradient3(p.x * 2, p.y * 2, p.z * 2, seed + 3) * 2.0f);  // bedding planes
    Vec3 q = p * d * sc;
    if (q.y < -0.4f) q.y = -0.4f + (q.y + 0.4f) * 0.25f;
    v.px = q.x; v.py = q.y + 0.4f; v.pz = q.z;
  }
  fixWinding(m);
  m.recomputeNormals();
  bakeCavityAO(m, seed, 1.2f);
  out.opaque = std::move(m);
  out.finalize();
  return out;
}

PropMesh makeKarstSpire(uint32_t seed, int lod) {
  Rng rng(seed * 8831u + 1u);
  PropMesh out;
  MeshData& m = out.opaque;
  const float H = rng.range(26.0f, 44.0f);
  const float baseR = rng.range(4.0f, 6.0f);
  const int sides = lod == 0 ? 28 : (lod == 1 ? 16 : 9);
  const int rings = lod == 0 ? 40 : (lod == 1 ? 18 : 8);
  const float flutes = static_cast<float>(rng.rangeInt(5, 9));
  const Vec3 lean{rng.range(-0.08f, 0.08f), 0, rng.range(-0.08f, 0.08f)};
  const uint32_t stride = static_cast<uint32_t>(sides + 1);
  for (int r = 0; r <= rings; ++r) {
    const float t = static_cast<float>(r) / rings;
    const float y = H * t;
    // Profile: wide base, waisted middle, rounded weathered top.
    float prof = lerp(1.0f, 0.62f, smoothstep(0.0f, 0.5f, t)) * (1.0f - 0.85f * smoothstep(0.86f, 1.0f, t));
    prof *= 1.0f + 0.12f * noise::gradient2(t * 6.0f, 0.3f, seed + 2);
    for (int s = 0; s <= sides; ++s) {
      const float a = kTwoPi * static_cast<float>(s % sides) / sides;
      const Vec3 dir{std::cos(a), 0, std::sin(a)};
      // Vertical solution flutes (rainwater erosion) + horizontal bedding notches.
      const float flute = 0.1f * std::fabs(std::sin(a * flutes * 0.5f + noise::gradient2(t * 3.0f, a, seed) * 1.2f));
      const float notch = 0.06f * smoothstep(0.6f, 1.0f, std::sin(y * 0.9f + noise::gradient2(a * 2.0f, t * 4.0f, seed + 4) * 2.0f));
      const float rough = 0.12f * noise::fbm3(dir.x * 2.0f, y * 0.15f, dir.z * 2.0f, seed + 7, 3);
      const float rad = baseR * prof * (1.0f - flute - notch + rough);
      const Vec3 p = dir * rad + Vec3{0, y, 0} + lean * (y * y / H);
      const float ao = 0.55f + 0.45f * (1.0f - flute * 6.0f) - 0.25f * (1.0f - smoothstep(0.0f, 0.12f, t));
      m.addVertex(p, dir, {static_cast<float>(s) / sides * 6.0f, y * 0.25f}, ao);
    }
  }
  for (uint32_t r = 0; r < static_cast<uint32_t>(rings); ++r)
    for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) {
      const uint32_t a = r * stride + s;
      m.addQuad(a, a + stride, a + stride + 1, a + 1);
    }
  // Top cap.
  const uint32_t top = m.addVertex(Vec3{0, H * 1.005f, 0} + lean * H, {0, 1, 0}, {0.5f, 0.5f}, 1.0f);
  const uint32_t last = static_cast<uint32_t>(rings) * stride;
  for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) m.addTriangle(last + s, last + s + 1, top);
  fixWinding(m);
  m.recomputeNormals();
  out.finalize();
  return out;
}

}  // namespace aaa::procgen
