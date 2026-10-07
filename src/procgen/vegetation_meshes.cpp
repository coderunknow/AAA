#include "procgen/vegetation_meshes.h"

#include <cmath>

#include "core/noise.h"
#include "core/rng.h"

namespace aaa::procgen {
namespace {

Vec2 tileUv(FoliageTile t, float u, float v) {
  const int i = static_cast<int>(t);
  return {(static_cast<float>(i % 2) + clampf(u, 0.002f, 0.998f)) * 0.5f,
          (static_cast<float>(i / 2) + clampf(v, 0.002f, 0.998f)) * 0.5f};
}

// A foliage card: quad centred at `c` spanned by axes `ax` (width) and `ay` (height).
// Normals are bent toward `normalBias` (e.g. away from the crown centre) so cards
// shade like a volume instead of flat planes.
void addCard(MeshData& m, Vec3 c, Vec3 ax, Vec3 ay, FoliageTile tile, Vec3 normalBias, float ao0, float ao1,
             float wind0, float wind1, float phase) {
  const Vec3 n = normalize(normalBias);
  const Vec3 p[4] = {c - ax - ay, c + ax - ay, c + ax + ay, c - ax + ay};
  const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
  uint32_t idx[4];
  for (int i = 0; i < 4; ++i) {
    const bool top = i >= 2;
    idx[i] = m.addVertex(p[i], n, tileUv(tile, uv[i][0], uv[i][1]), top ? ao1 : ao0, top ? wind1 : wind0, 1.0f, phase);
  }
  m.addQuad(idx[0], idx[3], idx[2], idx[1]);
}

std::vector<Vec3> bentLine(Vec3 start, Vec3 dir, float len, int segs, Vec3 bend, float bendPow) {
  std::vector<Vec3> pts;
  for (int i = 0; i <= segs; ++i) {
    const float t = static_cast<float>(i) / segs;
    pts.push_back(start + dir * (len * t) + bend * std::pow(t, bendPow));
  }
  return pts;
}

}  // namespace

PropMesh makeMountainPine(uint32_t seed, int lod) {
  Rng rng(seed * 92821u + 17u);
  PropMesh out;
  const float H = rng.range(13.0f, 17.0f);
  const int trunkSides = lod == 0 ? 9 : (lod == 1 ? 6 : 4);
  const int trunkRings = lod == 0 ? 14 : (lod == 1 ? 7 : 4);
  // Wind-swept lean: the whole crown drifts to one side, typical of exposed mountain pines.
  const float leanYaw = rng.range(0.0f, kTwoPi);
  const Vec3 leanDir{std::sin(leanYaw), 0, std::cos(leanYaw)};
  const float lean = rng.range(0.8f, 2.6f);

  std::vector<Vec3> trunk;
  std::vector<float> radii;
  for (int i = 0; i <= trunkRings; ++i) {
    const float t = static_cast<float>(i) / trunkRings;
    const float wobble = 0.35f * std::sin(t * 5.0f + seed) * t;
    trunk.push_back(Vec3{0, H * t, 0} + leanDir * (lean * t * t) + Vec3{wobble, 0, wobble * 0.5f});
    radii.push_back(lerp(0.36f, 0.07f, std::pow(t, 0.8f)) * (i == 0 ? 1.35f : 1.0f));  // root flare
  }
  addTube(out.opaque, trunk, radii, trunkSides, 0.6f, 0.0f, 0.35f, rng.nextFloat(), true);

  // Branch whorls in the upper part; long, near-horizontal, drooping at the tips.
  const int branches = lod == 0 ? 15 : (lod == 1 ? 10 : 6);
  const Vec3 crownCentre = Vec3{0, H * 0.82f, 0} + leanDir * (lean * 0.7f);
  for (int b = 0; b < branches; ++b) {
    const float t = lerp(0.48f, 0.97f, (b + rng.range(0.0f, 0.8f)) / branches);
    const int ti = std::min(trunkRings - 1, static_cast<int>(t * trunkRings));
    const float ft = t * trunkRings - ti;
    const Vec3 origin = lerp(trunk[ti], trunk[ti + 1], ft);
    float yaw = rng.range(0.0f, kTwoPi);
    // Bias branches toward the lean side (asymmetric crown).
    if (rng.nextFloat() < 0.45f) yaw = leanYaw + rng.range(-0.9f, 0.9f);
    const Vec3 dir = normalize(Vec3{std::sin(yaw), rng.range(-0.05f, 0.28f), std::cos(yaw)});
    const float len = lerp(5.2f, 2.2f, (t - 0.48f) / 0.5f) * rng.range(0.75f, 1.2f) * (dot(dir, leanDir) > 0.3f ? 1.25f : 1.0f);
    const float phase = rng.nextFloat();
    const int segs = lod == 0 ? 5 : 3;
    const std::vector<Vec3> line = bentLine(origin, dir, len, segs, Vec3{0, -0.6f * len * 0.25f, 0}, 2.0f);
    if (lod < 2) {
      std::vector<float> br;
      for (int i = 0; i <= segs; ++i) br.push_back(lerp(0.09f, 0.025f, static_cast<float>(i) / segs) * std::sqrt(len / 4.0f));
      addTube(out.opaque, line, br, lod == 0 ? 5 : 3, 0.8f, 0.3f, 0.8f, phase, false);
    }
    // Flat needle pads along the outer part of the branch.
    const int pads = lod == 0 ? 5 : (lod == 1 ? 3 : 2);
    for (int p = 0; p < pads; ++p) {
      const float u = lerp(0.35f, 1.0f, static_cast<float>(p) / std::max(1, pads - 1));
      const int si = std::min(segs - 1, static_cast<int>(u * segs));
      const Vec3 c = lerp(line[si], line[si + 1], u * segs - si) + Vec3{0, rng.range(0.05f, 0.3f), 0};
      const float r = rng.range(1.0f, 1.6f) * (lod == 2 ? 1.6f : (lod == 1 ? 1.25f : 1.0f)) * (0.7f + 0.3f * u);
      const Vec3 outward = normalize(c - crownCentre + Vec3{0, 1.2f, 0});
      const float ao = clampf(0.55f + 0.45f * u, 0.0f, 1.0f);
      const float a = rng.range(0.0f, kPi);
      const Vec3 ax{std::cos(a) * r, rng.range(-0.08f, 0.08f) * r, std::sin(a) * r};
      const Vec3 az = normalize(cross(Vec3{0, 1, 0}, ax)) * r * 0.85f;
      // Horizontal pad (main silhouette from below/side) ...
      addCard(out.foliage, c, ax, az + Vec3{0, 0.12f * r, 0}, FoliageTile::PineNeedles, outward, ao * 0.85f, ao, 0.6f, 1.0f, phase);
      // ... plus a tilted card for volume when seen edge-on.
      if (lod < 2)
        addCard(out.foliage, c + Vec3{0, 0.15f * r, 0}, ax * 0.9f, Vec3{0, 0.42f * r, 0} + az * 0.25f, FoliageTile::PineNeedles,
                outward, ao * 0.75f, ao, 0.6f, 1.0f, phase);
    }
  }
  // Crown cap so the top reads as a dense mass.
  const int caps = lod == 2 ? 2 : 4;
  for (int i = 0; i < caps; ++i) {
    const float a = rng.range(0.0f, kPi);
    const float r = rng.range(1.6f, 2.4f);
    const Vec3 ax{std::cos(a) * r, 0, std::sin(a) * r};
    const Vec3 az = normalize(cross(Vec3{0, 1, 0}, ax)) * r;
    addCard(out.foliage, trunk.back() + Vec3{rng.range(-0.6f, 0.6f), rng.range(-0.4f, 0.3f), rng.range(-0.6f, 0.6f)}, ax,
            az, FoliageTile::PineNeedles, Vec3{0, 1, 0}, 0.9f, 1.0f, 0.6f, 0.9f, rng.nextFloat());
  }
  out.finalize();
  return out;
}

PropMesh makeYoungPine(uint32_t seed, int lod) {
  Rng rng(seed * 7121u + 3u);
  PropMesh out;
  const float H = rng.range(6.0f, 8.0f);
  std::vector<Vec3> trunk;
  std::vector<float> radii;
  const int rings = lod == 0 ? 6 : 3;
  for (int i = 0; i <= rings; ++i) {
    const float t = static_cast<float>(i) / rings;
    trunk.push_back({0.1f * std::sin(t * 3.0f + seed), H * t, 0});
    radii.push_back(lerp(0.16f, 0.03f, t));
  }
  addTube(out.opaque, trunk, radii, lod == 0 ? 6 : 4, 0.8f, 0.0f, 0.5f, rng.nextFloat(), true);
  const int whorls = lod == 0 ? 7 : (lod == 1 ? 5 : 3);
  for (int w = 0; w < whorls; ++w) {
    const float t = lerp(0.18f, 0.95f, static_cast<float>(w) / (whorls - 1));
    const float r = lerp(2.2f, 0.5f, t) * rng.range(0.85f, 1.15f);
    const Vec3 c{0.1f * std::sin(t * 3.0f + seed), H * t, 0};
    const int cards = lod == 2 ? 2 : 3;
    for (int k = 0; k < cards; ++k) {
      const float a = (k + rng.range(0.0f, 0.5f)) * kPi / cards;
      const Vec3 ax{std::cos(a) * r, 0, std::sin(a) * r};
      // Drooping cone-shaped tier: tilt each card down at the rim.
      const Vec3 ay = Vec3{0, 0.55f * r, 0};
      addCard(out.foliage, c + Vec3{0, -0.1f * r, 0}, ax, ay, FoliageTile::PineNeedles,
              Vec3{std::cos(a + 1.57f) * 0.5f, 1.0f, std::sin(a + 1.57f) * 0.5f}, 0.55f + 0.4f * t, 0.85f + 0.15f * t,
              0.4f, 0.9f, rng.nextFloat());
    }
  }
  out.finalize();
  return out;
}

PropMesh makeBambooClump(uint32_t seed, int lod) {
  Rng rng(seed * 3301u + 11u);
  PropMesh out;
  const int culms = lod == 0 ? rng.rangeInt(9, 14) : (lod == 1 ? 7 : 4);
  for (int c = 0; c < culms; ++c) {
    const float a = rng.range(0.0f, kTwoPi);
    const float rr = rng.range(0.0f, 0.9f);
    const Vec3 base{std::cos(a) * rr, 0, std::sin(a) * rr};
    const float H = rng.range(7.5f, 11.5f);
    const Vec3 lean = normalize(Vec3{base.x + rng.range(-0.3f, 0.3f), 0, base.z + rng.range(-0.3f, 0.3f)}) * rng.range(0.6f, 2.0f);
    const float radius = rng.range(0.035f, 0.06f);
    const int rings = lod == 0 ? 16 : (lod == 1 ? 8 : 4);
    std::vector<Vec3> line;
    std::vector<float> radii;
    for (int i = 0; i <= rings; ++i) {
      const float t = static_cast<float>(i) / rings;
      line.push_back(base + Vec3{0, H * t, 0} + lean * (t * t));
      // Node bulges every few rings for the characteristic segmented look.
      const float node = (lod == 0 && i % 2 == 0) ? 1.12f : 1.0f;
      radii.push_back(radius * lerp(1.0f, 0.55f, t) * node);
    }
    const float phase = rng.nextFloat();
    addTube(out.opaque, line, radii, lod == 0 ? 6 : 4, 1.0f / 0.4f, 0.0f, 1.0f, phase, true);
    // Leaf sprays on the upper half.
    const int sprays = lod == 0 ? 9 : (lod == 1 ? 5 : 3);
    for (int s = 0; s < sprays; ++s) {
      const float t = lerp(0.45f, 1.0f, (s + rng.nextFloat()) / sprays);
      const int i = std::min(rings - 1, static_cast<int>(t * rings));
      const Vec3 p = lerp(line[i], line[i + 1], t * rings - i);
      const float ya = rng.range(0.0f, kTwoPi);
      const Vec3 out1{std::sin(ya), 0, std::cos(ya)};
      const float sz = rng.range(0.7f, 1.1f) * (lod == 2 ? 1.8f : (lod == 1 ? 1.3f : 1.0f));
      addCard(out.foliage, p + out1 * (0.5f * sz) + Vec3{0, -0.15f * sz, 0}, out1 * (0.6f * sz) + Vec3{0, -0.25f * sz, 0},
              Vec3{std::cos(ya), 0.3f, -std::sin(ya)} * (0.55f * sz), FoliageTile::BambooLeaves, out1 + Vec3{0, 0.8f, 0},
              0.55f + 0.45f * t, 0.65f + 0.35f * t, 0.6f * t, 1.0f, phase);
    }
  }
  out.finalize();
  return out;
}

PropMesh makeFern(uint32_t seed, int lod) {
  Rng rng(seed * 1931u + 5u);
  PropMesh out;
  const int fronds = lod == 0 ? rng.rangeInt(7, 10) : 4;
  for (int f = 0; f < fronds; ++f) {
    const float ya = (f + rng.range(-0.3f, 0.3f)) * kTwoPi / fronds;
    const Vec3 dir{std::sin(ya), 0, std::cos(ya)};
    const Vec3 side{std::cos(ya), 0, -std::sin(ya)};
    const float len = rng.range(0.7f, 1.1f);
    const float rise = rng.range(0.35f, 0.6f);
    const int segs = lod == 0 ? 4 : 2;
    const float phase = rng.nextFloat();
    uint32_t prevL = 0, prevR = 0;
    for (int s = 0; s <= segs; ++s) {
      const float t = static_cast<float>(s) / segs;
      // Arching frond: rises then droops.
      const Vec3 c = dir * (len * t) + Vec3{0, rise * std::sin(t * kPi * 0.85f), 0};
      const float w = 0.16f * std::sin(std::fmin(1.0f, t * 1.4f + 0.15f) * kPi * 0.9f) + 0.02f;
      const Vec3 n = normalize(Vec3{0, 1, 0} + dir * (t - 0.4f));
      const float v = 1.0f - t;
      const uint32_t l = out.foliage.addVertex(c - side * w, n, tileUv(FoliageTile::FernFrond, 0, v), 0.5f + 0.5f * t, t, 1.0f, phase);
      const uint32_t r = out.foliage.addVertex(c + side * w, n, tileUv(FoliageTile::FernFrond, 1, v), 0.5f + 0.5f * t, t, 1.0f, phase);
      if (s > 0) out.foliage.addQuad(prevL, l, r, prevR);
      prevL = l; prevR = r;
    }
  }
  out.finalize();
  return out;
}

PropMesh makeGrassClump(uint32_t seed, int lod) {
  // A loose tussock: cards fan outward from a small base at varied heights, so the clump
  // reads as a soft mound of blades from every angle rather than a crossed-quad cylinder.
  Rng rng(seed * 4409u + 1u);
  PropMesh out;
  const int cards = lod == 0 ? 7 : lod == 1 ? 4 : 2;
  for (int c = 0; c < cards; ++c) {
    const float a = (c + rng.range(-0.3f, 0.3f)) * kPi / cards;
    const float h = rng.range(0.22f, 0.36f) * (c % 3 == 0 ? 1.25f : 1.0f);  // half height
    const float w = rng.range(0.16f, 0.26f);
    const float r = rng.range(0.0f, 0.22f);
    const float oa = rng.range(0.0f, kTwoPi);
    const Vec3 base{std::cos(oa) * r, 0, std::sin(oa) * r};
    const Vec3 ax{std::cos(a) * w, 0, std::sin(a) * w};
    // Lean away from the clump centre (and a little randomly) so tops splay outward.
    const Vec3 lean = (r > 0.02f ? normalize(base) : Vec3{std::cos(oa), 0, std::sin(oa)}) * rng.range(0.05f, 0.14f);
    const Vec3 ay{lean.x, h, lean.z};
    addCard(out.foliage, base + ay, ax, ay, FoliageTile::GrassBlades, Vec3{lean.x * 2.0f, 1, lean.z * 2.0f}, 0.25f,
            1.0f, 0.0f, 1.0f, rng.nextFloat());
  }
  out.finalize();
  return out;
}

PropMesh makeFallenLog(uint32_t seed, int lod) {
  Rng rng(seed * 6007u + 9u);
  PropMesh out;
  const float len = rng.range(4.0f, 5.5f);
  const int rings = lod == 0 ? 10 : 4;
  std::vector<Vec3> line;
  std::vector<float> radii;
  for (int i = 0; i <= rings; ++i) {
    const float t = static_cast<float>(i) / rings;
    line.push_back({0.15f * std::sin(t * 3.0f + seed), 0.3f + 0.05f * std::sin(t * 7.0f), (t - 0.5f) * len});
    const float jag = (i == 0 || i == rings) ? 0.85f : 1.0f;
    radii.push_back(lerp(0.34f, 0.24f, t) * jag);
  }
  addTube(out.opaque, line, radii, lod == 0 ? 9 : 5, 0.6f, 0.0f, 0.0f, 0.0f, true);
  out.finalize();
  return out;
}

}  // namespace aaa::procgen
