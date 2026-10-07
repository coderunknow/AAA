#include "procgen/structure_meshes.h"

#include <cmath>

#include "core/rng.h"
#include "procgen/primitive_meshes.h"
#include "world/heightfield.h"
#include "world/shrine.h"

namespace aaa::procgen {

void appendTransformed(MeshData& dst, const MeshData& src, const Mat4& xf) {
  const Vec3 c0{xf.m[0], xf.m[1], xf.m[2]}, c1{xf.m[4], xf.m[5], xf.m[6]}, c2{xf.m[8], xf.m[9], xf.m[10]};
  const bool mirrored = dot(cross(c0, c1), c2) < 0.0f;
  const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
  for (Vertex v : src.vertices) {
    const Vec3 p = xf.transformPoint({v.px, v.py, v.pz});
    // Inverse-transpose for non-uniform scale: scale normal components by 1/len^2 of each axis.
    const Vec3 nl{v.nx / std::fmax(dot(c0, c0), 1e-8f), v.ny / std::fmax(dot(c1, c1), 1e-8f),
                  v.nz / std::fmax(dot(c2, c2), 1e-8f)};
    const Vec3 n = normalize(xf.transformDir(nl));
    v.px = p.x; v.py = p.y; v.pz = p.z;
    v.nx = n.x; v.ny = n.y; v.nz = n.z;
    dst.vertices.push_back(v);
  }
  for (size_t i = 0; i + 2 < src.indices.size(); i += 3) {
    const uint32_t a = base + src.indices[i], b = base + src.indices[i + 1], c = base + src.indices[i + 2];
    if (mirrored) dst.addTriangle(a, c, b);
    else dst.addTriangle(a, b, c);
  }
}

void addBox(MeshData& m, const Mat4& xf, float uvScale) {
  const Vec3 corners[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                           {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  Vec3 w[8];
  for (int i = 0; i < 8; ++i) w[i] = xf.transformPoint(corners[i]);
  const int faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
  const Vec3 centre = xf.transformPoint({0, 0, 0});
  for (const auto& f : faces) {
    const Vec3 a = w[f[0]], b = w[f[1]], c = w[f[2]], d = w[f[3]];
    Vec3 n = normalize(cross(b - a, c - a));
    const Vec3 faceC = (a + b + c + d) * 0.25f;
    const bool flip = dot(n, faceC - centre) < 0.0f;
    if (flip) n = -n;
    const float su = length(b - a) * uvScale, sv = length(d - a) * uvScale;
    const uint32_t i0 = m.addVertex(a, n, {0, 0}), i1 = m.addVertex(b, n, {su, 0}), i2 = m.addVertex(c, n, {su, sv}),
                   i3 = m.addVertex(d, n, {0, sv});
    if (flip) m.addQuad(i0, i3, i2, i1);
    else m.addQuad(i0, i1, i2, i3);
  }
}

namespace {

// Shrine-local frame -> world matrix (local y is absolute world height).
Mat4 shrineFrame(const ShrineLayout& s) {
  const float sn = std::sin(s.yaw), cs = std::cos(s.yaw);
  Mat4 m = Mat4::identity();
  m.m[0] = cs; m.m[1] = 0; m.m[2] = -sn;   // local x -> world
  m.m[8] = sn; m.m[9] = 0; m.m[10] = cs;   // local z -> world
  m.m[12] = s.center.x; m.m[13] = 0; m.m[14] = s.center.y;
  return m;
}

void addTubeLine(MeshData& m, Vec3 a, Vec3 b, float r0, float r1, int sides, bool cap) {
  addTube(m, {a, b}, {r0, r1}, sides, 1.0f, 0.0f, 0.0f, 0.0f, cap);
}

// Hipped roof with a concave profile, upturned corners and barrel-tile corrugation.
// `sag` collapses the front-right corner where the pillar has failed.
void buildRoof(MeshData& m, const Mat4& frame, float eaveY, float rise, float ex, float ez, uint32_t seed) {
  const int nx = 120, nz = 52;
  const float thickness = 0.16f;
  auto height = [&](float lx, float lz) {
    const float d = std::fmax(std::fabs(lz), std::fabs(lx) - (ex - ez)) / ez;  // 0 ridge .. 1 eave
    const float dd = clampf(d, 0.0f, 1.0f);
    float y = eaveY + rise * std::pow(1.0f - dd, 1.55f);
    const float corner = std::pow(clampf(std::fabs(lx) / ex, 0, 1) * clampf(std::fabs(lz) / ez, 0, 1), 7.0f);
    y += 0.55f * corner;
    // Barrel tiles run down the slope; corrugate across it.
    const float across = std::fabs(lz) > std::fabs(lx) - (ex - ez) ? lx : lz;
    y += 0.035f * std::fabs(std::sin(across * kPi / 0.3f)) * smoothstep(0.02f, 0.12f, dd);
    // Collapse: the corner over the broken pillar sags.
    const float sag = smoothstep(0.15f, 1.0f, lx / ex) * smoothstep(0.1f, 1.0f, lz / ez);
    y -= 1.35f * sag * sag;
    return y;
  };
  auto hole = [&](int ix, int iz) {
    const float lx = -ex + 2.0f * ex * (ix + 0.5f) / nx, lz = -ez + 2.0f * ez * (iz + 0.5f) / nz;
    const float region = smoothstep(0.35f, 0.7f, lx / ex) * smoothstep(0.2f, 0.6f, lz / ez);
    const uint32_t h = hash2i(ix / 6, iz / 4, seed);
    return region > 0.2f && hashToFloat(h) < region * 0.85f;
  };
  for (int surface = 0; surface < 2; ++surface) {
    const uint32_t base = static_cast<uint32_t>(m.vertices.size());
    for (int iz = 0; iz <= nz; ++iz)
      for (int ix = 0; ix <= nx; ++ix) {
        const float lx = -ex + 2.0f * ex * ix / nx, lz = -ez + 2.0f * ez * iz / nz;
        const float y = height(lx, lz) - (surface == 1 ? thickness : 0.0f);
        const Vec3 p = frame.transformPoint({lx, y, lz});
        m.addVertex(p, {0, surface == 0 ? 1.0f : -1.0f, 0}, {lx * 0.5f, lz * 0.5f});
      }
    for (int iz = 0; iz < nz; ++iz)
      for (int ix = 0; ix < nx; ++ix) {
        if (hole(ix, iz)) continue;
        const uint32_t a = base + iz * (nx + 1) + ix, b = a + 1, c = a + nx + 2, d = a + nx + 1;
        // Top faces up, bottom faces down; orientation verified numerically below.
        const Vec3 pa{m.vertices[a].px, m.vertices[a].py, m.vertices[a].pz};
        const Vec3 pb{m.vertices[b].px, m.vertices[b].py, m.vertices[b].pz};
        const Vec3 pc{m.vertices[c].px, m.vertices[c].py, m.vertices[c].pz};
        const bool up = cross(pb - pa, pc - pa).y > 0.0f;
        if (up == (surface == 0)) m.addQuad(a, b, c, d);
        else m.addQuad(a, d, c, b);
      }
  }
}
}  // namespace

std::vector<MaterialMesh> buildShrineMeshes(const ShrineLayout& s, const Heightfield& hf) {
  MeshData stone, moss, wood, lacquer, roof;
  const Mat4 F = shrineFrame(s);
  const float top = s.platformTop;
  auto box = [&](MeshData& m, Vec3 c, Vec3 h, float uv = 0.5f) {
    addBox(m, F * Mat4::translation(c) * Mat4::scale(h), uv);
  };
  auto boxR = [&](MeshData& m, Vec3 c, Vec3 h, const Mat4& rot, float uv = 0.5f) {
    addBox(m, F * Mat4::translation(c) * rot * Mat4::scale(h), uv);
  };
  auto local = [&](float x, float y, float z) { return F.transformPoint({x, y, z}); };
  Rng rng(0x5a17e);

  // Platform in coursed blocks: the walkable volumes plus a chamfered upper course.
  for (const ShrineBox& b : s.walkable) box(stone, b.center, b.half, 0.6f);
  for (int i = 0; i < 14; ++i) {  // a few displaced edge blocks break the silhouette
    const float side = rng.nextFloat() < 0.5f ? -1.0f : 1.0f;
    const bool alongX = rng.nextFloat() < 0.5f;
    const float t = rng.range(-0.9f, 0.9f);
    const Vec3 c = alongX ? Vec3{t * 5.0f, top - 0.18f, side * 4.25f} : Vec3{side * 5.25f, top - 0.18f, t * 4.0f};
    boxR(moss, c, {rng.range(0.25f, 0.45f), 0.2f, rng.range(0.2f, 0.3f)}, Mat4::rotationY(rng.range(-0.15f, 0.15f)));
  }

  // Pillars on stone bases. Front-right pillar is snapped; its upper half lies on the platform.
  const float px = 3.6f, pz = 2.8f;
  const Vec3 pillars[6] = {{-px, 0, -pz}, {px, 0, -pz}, {-px, 0, pz}, {px, 0, pz}, {-1.2f, 0, -pz}, {1.2f, 0, -pz}};
  for (int i = 0; i < 6; ++i) {
    const Vec3 p = pillars[i];
    box(stone, {p.x, top + 0.12f, p.z}, {0.3f, 0.12f, 0.3f});
    const bool broken = i == 3;
    const float h = broken ? 1.25f : 3.0f;
    addTubeLine(wood, local(p.x, top + 0.24f, p.z), local(p.x + (broken ? 0.05f : 0.0f), top + h, p.z), 0.17f, 0.15f, 12,
                true);
  }
  addTubeLine(wood, local(2.2f, top + 0.16f, 3.9f), local(4.6f, top - 0.05f, 1.4f), 0.15f, 0.14f, 12, true);

  // Beams (lintels) tying the pillar heads; the front-right ones droop with the collapse.
  box(wood, {0.0f, top + 2.95f, -pz}, {3.8f, 0.13f, 0.12f});
  box(wood, {-px, top + 2.95f, 0.0f}, {0.12f, 0.13f, 3.0f});
  boxR(wood, {0.0f, top + 2.55f, pz}, {3.8f, 0.13f, 0.12f}, Mat4::rotationZ(-0.12f));
  boxR(wood, {px, top + 2.45f, 0.0f}, {0.12f, 0.13f, 3.0f}, Mat4::rotationX(0.16f));

  // Roof.
  buildRoof(roof, F, top + 3.05f, 1.55f, 4.8f, 3.95f, 0x7007u);
  roof.recomputeNormals();  // smooth shading across the curved, corrugated surface

  // Low back wall, altar slab and an old incense burner.
  box(moss, {0.0f, top + 0.55f, -3.0f}, {3.7f, 0.55f, 0.22f}, 0.7f);
  box(stone, {0.0f, top + 0.45f, -2.0f}, {0.72f, 0.45f, 0.4f});
  box(stone, {0.0f, top + 0.94f, -2.0f}, {0.85f, 0.05f, 0.5f});
  addTubeLine(lacquer, local(0.0f, top + 0.99f, -1.95f), local(0.0f, top + 1.2f, -1.95f), 0.13f, 0.16f, 14, true);

  // Stone lanterns flanking the approach (the right one leans, undermined by roots).
  for (int i = 0; i < 2; ++i) {
    const float x = i == 0 ? -2.9f : 2.9f;
    const Vec3 w = local(x, 0.0f, 6.4f);
    const float g = hf.height(w.x, w.z) - 0.05f;
    const Mat4 lean = i == 1 ? Mat4::rotationZ(0.13f) : Mat4::identity();
    const Mat4 L = F * Mat4::translation({x, g, 6.4f}) * lean;
    auto lbox = [&](MeshData& m, Vec3 c, Vec3 h) { addBox(m, L * Mat4::translation(c) * Mat4::scale(h), 0.8f); };
    lbox(moss, {0, 0.12f, 0}, {0.34f, 0.12f, 0.34f});
    lbox(stone, {0, 0.6f, 0}, {0.12f, 0.36f, 0.12f});
    lbox(stone, {0, 1.02f, 0}, {0.25f, 0.06f, 0.25f});
    lbox(stone, {0, 1.24f, 0}, {0.2f, 0.16f, 0.2f});
    lbox(moss, {0, 1.47f, 0}, {0.38f, 0.07f, 0.38f});
    lbox(stone, {0, 1.62f, 0}, {0.16f, 0.08f, 0.16f});
    lbox(stone, {0, 1.76f, 0}, {0.07f, 0.06f, 0.07f});
  }

  // Gate down the approach: weathered lacquered posts, double lintel with lifted ends.
  {
    const float gy = 0.5f * (hf.height(s.gateLeft.x, s.gateLeft.y) + hf.height(s.gateRight.x, s.gateRight.y));
    const float gz = 12.5f;
    for (float x : {-2.0f, 2.0f}) {
      addTubeLine(lacquer, local(x, gy - 0.4f, gz), local(x, gy + 3.75f, gz), 0.19f, 0.16f, 12, false);
      box(stone, {x, gy + 0.05f, gz}, {0.32f, 0.22f, 0.32f});
    }
    box(lacquer, {0.0f, gy + 3.0f, gz}, {2.35f, 0.1f, 0.11f});
    box(wood, {0.0f, gy + 3.62f, gz}, {2.75f, 0.13f, 0.2f});
    for (float sgn : {-1.0f, 1.0f})
      boxR(wood, {sgn * 3.05f, gy + 3.72f, gz}, {0.4f, 0.11f, 0.19f}, Mat4::rotationZ(sgn * 0.32f));
    box(lacquer, {0.0f, gy + 3.3f, gz}, {0.22f, 0.22f, 0.06f});  // blank name tablet
  }

  // Scattered fallen tiles and stones around the collapsed corner.
  for (int i = 0; i < 18; ++i) {
    const float x = rng.range(2.0f, 6.5f), z = rng.range(1.0f, 6.0f);
    const Vec3 w = local(x, 0.0f, z);
    const float g = std::fmax(hf.height(w.x, w.z), s.surfaceHeight(w.x, w.z));
    boxR(i % 3 == 0 ? moss : roof, {x, g + 0.03f, z}, {rng.range(0.12f, 0.22f), 0.03f, rng.range(0.1f, 0.16f)},
         Mat4::rotationY(rng.range(0.0f, kTwoPi)) * Mat4::rotationX(rng.range(-0.2f, 0.2f)));
  }

  std::vector<MaterialMesh> out;
  auto emit = [&](StructMaterial mat, MeshData& m) {
    if (m.empty()) return;
    m.computeBounds();
    out.push_back({mat, std::move(m)});
  };
  emit(StructMaterial::Stone, stone);
  emit(StructMaterial::MossStone, moss);
  emit(StructMaterial::Wood, wood);
  emit(StructMaterial::Lacquer, lacquer);
  emit(StructMaterial::RoofTile, roof);
  return out;
}

std::vector<PickupMeshPart> buildPickupMesh(int kind, int variant) {
  std::vector<PickupMeshPart> parts;
  Rng rng(hashCombine(0xf0a6u + kind * 31u, variant));
  const MeshData sphere = makeSphere(10, 7);
  const MeshData sphereHi = makeSphere(14, 9);
  switch (kind) {
    case 0: {  // Deadwood: a small pile of grey, barkless branches
      MeshData m;
      const int n = 3 + variant % 2;
      for (int i = 0; i < n; ++i) {
        const float a = rng.range(0.0f, kPi), len = rng.range(0.75f, 1.2f);
        const Vec3 dir{std::cos(a), 0.0f, std::sin(a)};
        const Vec3 c{rng.range(-0.15f, 0.15f), 0.05f + 0.07f * i, rng.range(-0.15f, 0.15f)};
        const Vec3 a0 = c - dir * (len * 0.5f), a1 = c + dir * (len * 0.5f) + Vec3{0, rng.range(-0.04f, 0.06f), 0};
        addTube(m, {a0, (a0 + a1) * 0.5f + Vec3{0, 0.02f, 0}, a1}, {0.05f, 0.042f, 0.03f}, 7, 1.0f, 0, 0, 0, true);
        // A forked twig.
        const Vec3 mid = lerp(a0, a1, 0.6f);
        addTube(m, {mid, mid + Vec3{-dir.z, 0.12f, dir.x} * 0.3f + dir * 0.15f}, {0.025f, 0.012f}, 5, 1.0f, 0, 0, 0, true);
      }
      parts.push_back({StructMaterial::Deadwood, 0, std::move(m)});
      break;
    }
    case 1: {  // Flint: a few dark, glassy, angular nodules on a pale limestone slab
      MeshData f, slab;
      addBox(slab, Mat4::translation({0, 0.03f, 0}) * Mat4::rotationY(rng.range(0.0f, 1.0f)) *
                       Mat4::scale({0.34f, 0.05f, 0.26f}), 1.5f);
      const int n = 2 + variant % 2;
      for (int i = 0; i < n; ++i) {
        const float s = rng.range(0.07f, 0.11f);
        addBox(f, Mat4::translation({rng.range(-0.18f, 0.18f), 0.09f + s * 0.4f, rng.range(-0.14f, 0.14f)}) *
                      Mat4::rotationY(rng.range(0.0f, kTwoPi)) * Mat4::rotationX(rng.range(-0.6f, 0.6f)) *
                      Mat4::rotationZ(rng.range(-0.6f, 0.6f)) * Mat4::scale({s * 1.3f, s * 0.8f, s}),
               2.0f);
      }
      parts.push_back({StructMaterial::Stone, 0, std::move(slab)});
      parts.push_back({StructMaterial::Flint, 0, std::move(f)});
      break;
    }
    case 2: {  // Berry bush: clustered leaf masses + red berries (part 1)
      MeshData leaves, berries;
      const int n = 6 + variant;
      for (int i = 0; i < n; ++i) {
        const float a = i * 2.4f + rng.range(0.0f, 0.5f), r = (i == 0) ? 0.0f : rng.range(0.18f, 0.38f);
        const float s = rng.range(0.24f, 0.36f);
        appendTransformed(leaves, sphereHi,
                          Mat4::translation({std::sin(a) * r, 0.3f + rng.range(0.0f, 0.25f), std::cos(a) * r}) *
                              Mat4::scale({s, s * 0.8f, s}));
      }
      for (int i = 0; i < 34; ++i) {
        // Berries sit on the outer surface of the leaf masses, more on the sunny top.
        const float a = rng.range(0.0f, kTwoPi), el = rng.range(-0.2f, 1.1f);
        const Vec3 dir = normalize(Vec3{std::cos(a) * std::cos(el), std::sin(el) * 0.8f, std::sin(a) * std::cos(el)});
        const Vec3 p = Vec3{0, 0.36f, 0} + dir * rng.range(0.42f, 0.5f);
        const float s = rng.range(0.028f, 0.04f);
        appendTransformed(berries, sphere, Mat4::translation(p) * Mat4::scale({s, s, s}));
      }
      parts.push_back({StructMaterial::Leaf, 0, std::move(leaves)});
      parts.push_back({StructMaterial::Berry, 1, std::move(berries)});
      break;
    }
    default: {  // Mushrooms: a small cluster of brown caps (part 1 — gone once picked)
      MeshData caps, stems;
      const int n = 3 + variant;
      for (int i = 0; i < n; ++i) {
        const float a = rng.range(0.0f, kTwoPi), r = rng.range(0.0f, 0.18f);
        const float h = rng.range(0.06f, 0.12f), cr = rng.range(0.05f, 0.085f);
        const Vec3 b{std::sin(a) * r, 0.0f, std::cos(a) * r};
        const Vec3 tilt{rng.range(-0.02f, 0.02f), 0.0f, rng.range(-0.02f, 0.02f)};
        addTube(stems, {b, b + Vec3{0, h, 0} + tilt}, {cr * 0.32f, cr * 0.26f}, 7, 1.0f, 0, 0, 0, false);
        appendTransformed(caps, sphere, Mat4::translation(b + Vec3{0, h, 0} + tilt) * Mat4::scale({cr, cr * 0.45f, cr}));
      }
      parts.push_back({StructMaterial::MushStem, 1, std::move(stems)});
      parts.push_back({StructMaterial::MushCap, 1, std::move(caps)});
      break;
    }
  }
  for (PickupMeshPart& p : parts) p.mesh.computeBounds();
  return parts;
}

std::vector<PickupMeshPart> buildCampfireMesh() {
  std::vector<PickupMeshPart> parts;
  Rng rng(0xf17eu);
  const MeshData sphere = makeSphere(10, 7);
  MeshData stones, logs, embers, ash;
  for (int i = 0; i < 10; ++i) {
    const float a = i * kTwoPi / 10.0f + rng.range(-0.1f, 0.1f);
    const float s = rng.range(0.1f, 0.14f);
    appendTransformed(stones, sphere,
                      Mat4::translation({std::sin(a) * 0.5f, 0.05f, std::cos(a) * 0.5f}) * Mat4::rotationY(a) *
                          Mat4::scale({s * 1.25f, s * 0.75f, s}));
  }
  // Teepee of split logs leaning together.
  for (int i = 0; i < 5; ++i) {
    const float a = i * kTwoPi / 5.0f + 0.3f;
    const Vec3 foot{std::sin(a) * 0.36f, 0.02f, std::cos(a) * 0.36f};
    addTube(logs, {foot, Vec3{std::sin(a) * 0.05f, 0.42f, std::cos(a) * 0.05f}}, {0.045f, 0.03f}, 7, 1.0f, 0, 0, 0, true);
  }
  appendTransformed(embers, sphere, Mat4::translation({0, 0.0f, 0}) * Mat4::scale({0.3f, 0.06f, 0.3f}));
  for (int i = 0; i < 7; ++i) {  // glowing coals
    const float a = rng.range(0.0f, kTwoPi), r = rng.range(0.0f, 0.2f), s = rng.range(0.04f, 0.07f);
    appendTransformed(embers, sphere, Mat4::translation({std::sin(a) * r, 0.04f, std::cos(a) * r}) * Mat4::scale({s, s * 0.7f, s}));
  }
  appendTransformed(ash, sphere, Mat4::scale({0.38f, 0.04f, 0.38f}));
  parts.push_back({StructMaterial::Stone, 0, std::move(stones)});
  parts.push_back({StructMaterial::Charcoal, 0, std::move(logs)});
  parts.push_back({StructMaterial::Ember, 1, std::move(embers)});
  parts.push_back({StructMaterial::Ash, 2, std::move(ash)});
  for (PickupMeshPart& p : parts) p.mesh.computeBounds();
  return parts;
}

}  // namespace aaa::procgen
