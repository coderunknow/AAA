#include "procgen/primitive_meshes.h"
#include "procgen/rock_meshes.h"
#include "procgen/textures.h"
#include "procgen/vegetation_meshes.h"
#include "test.h"

using namespace aaa;
using namespace aaa::procgen;

namespace {
bool validMesh(const MeshData& m) {
  for (uint32_t i : m.indices)
    if (i >= m.vertices.size()) return false;
  for (const Vertex& v : m.vertices)
    if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz) || !std::isfinite(v.nx)) return false;
  return m.indices.size() % 3 == 0;
}
// Fraction of triangles whose winding faces away from the centroid (convex-ish meshes).
float outwardFraction(const MeshData& m) {
  Vec3 c{};
  for (auto& v : m.vertices) c += Vec3{v.px, v.py, v.pz};
  c = c / static_cast<float>(m.vertices.size());
  int out = 0, n = 0;
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    const auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &d = m.vertices[m.indices[i + 2]];
    const Vec3 pa{a.px, a.py, a.pz}, pb{b.px, b.py, b.pz}, pd{d.px, d.py, d.pz};
    const Vec3 nrm = cross(pb - pa, pd - pa);
    if (lengthSq(nrm) < 1e-12f) continue;
    out += dot(nrm, (pa + pb + pd) / 3.0f - c) > 0 ? 1 : 0;
    ++n;
  }
  return n ? static_cast<float>(out) / n : 0.0f;
}
}  // namespace

TEST_CASE("procgen: primitives are valid and wound outward") {
  for (const MeshData& m : {makeBox(), makeSphere(), makeCapsule()}) {
    CHECK(validMesh(m));
    CHECK(outwardFraction(m) > 0.99f);
  }
}

TEST_CASE("procgen: vegetation LODs reduce complexity and are deterministic") {
  using Gen = PropMesh (*)(uint32_t, int);
  for (Gen gen : {Gen(makeMountainPine), Gen(makeYoungPine), Gen(makeBambooClump), Gen(makeFern)}) {
    const PropMesh l0 = gen(3, 0), l1 = gen(3, 1), l2 = gen(3, 2), again = gen(3, 0);
    CHECK(validMesh(l0.opaque) && validMesh(l0.foliage));
    CHECK(l0.opaque.indices.size() + l0.foliage.indices.size() > l2.opaque.indices.size() + l2.foliage.indices.size());
    CHECK(l1.foliage.indices.size() <= l0.foliage.indices.size());
    CHECK(again.foliage.vertices.size() == l0.foliage.vertices.size());
  }
  const PropMesh pine = makeMountainPine(1, 0);
  CHECK(pine.boundsMax.y > 11.0f && pine.boundsMax.y < 20.0f);  // believable tree height
}

TEST_CASE("procgen: rocks are closed, outward and sit on the ground") {
  for (uint32_t s = 0; s < 4; ++s) {
    const PropMesh r = makeRock(s, 0), b = makeBoulder(s, 1), k = makeKarstSpire(s, 1);
    CHECK(validMesh(r.opaque) && validMesh(b.opaque) && validMesh(k.opaque));
    CHECK(outwardFraction(r.opaque) > 0.97f);
    CHECK(outwardFraction(b.opaque) > 0.97f);
    CHECK(r.boundsMin.y > -0.2f && r.boundsMin.y < 0.1f);
    CHECK(k.boundsMax.y > 25.0f);
  }
}

TEST_CASE("procgen: foliage atlas has coverage and mips preserve it") {
  const ImageRGBA8 atlas = makeFoliageAtlas(256, 9);
  size_t opaque = 0;
  for (size_t i = 3; i < atlas.pixels.size(); i += 4) opaque += atlas.pixels[i] > 127 ? 1 : 0;
  const float cov = static_cast<float>(opaque) / (atlas.pixels.size() / 4);
  CHECK(cov > 0.1f && cov < 0.9f);
  const auto mips = buildMipChain(atlas, true);
  CHECK(mips.size() == 9);
  // Coverage at mip 3 should stay within 35% of the base level.
  size_t o3 = 0;
  for (size_t i = 3; i < mips[3].pixels.size(); i += 4) o3 += mips[3].pixels[i] > 127 ? 1 : 0;
  const float cov3 = static_cast<float>(o3) / (mips[3].pixels.size() / 4);
  CHECK(std::fabs(cov3 - cov) < cov * 0.35f);
}
