#include "render/prop_renderer.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "procgen/rock_meshes.h"
#include "procgen/textures.h"
#include "procgen/vegetation_meshes.h"
#include "render/shader_library.h"
#include "world/world.h"

namespace aaa {
namespace {
using Clock = std::chrono::steady_clock;

struct KindStyle {
  int variants;
  float material;     // fs_prop material id
  float lod1, lod2, maxDist;
  Vec3 tintA, tintB;  // opaque albedo endpoints (linear)
  Vec3 leafA, leafB;  // foliage multipliers
  float alphaRef, translucency;
  bool big;           // casts shadows into the far cascade
};

// Linear-space albedo; values are deliberately low (forest bark/stone is dark).
const KindStyle kStyles[kPropKindCount] = {
    // Pine: reddish-grey bark, deep blue-green needles
    {4, 0.0f, 45.0f, 140.0f, 1000.0f, {0.13f, 0.09f, 0.07f}, {0.2f, 0.15f, 0.11f}, {0.52f, 0.58f, 0.58f}, {0.72f, 0.74f, 0.62f}, 0.45f, 0.55f, true},
    // Young pine: lighter, fresher needles
    {3, 0.0f, 35.0f, 110.0f, 700.0f, {0.14f, 0.1f, 0.075f}, {0.2f, 0.15f, 0.1f}, {0.62f, 0.68f, 0.6f}, {0.82f, 0.84f, 0.66f}, 0.45f, 0.6f, true},
    // Bamboo: green to straw-yellow culms
    {3, 3.0f, 35.0f, 100.0f, 600.0f, {0.16f, 0.24f, 0.08f}, {0.3f, 0.3f, 0.12f}, {0.66f, 0.74f, 0.58f}, {0.86f, 0.86f, 0.62f}, 0.4f, 0.75f, true},
    // Fern
    {3, 0.0f, 15.0f, 35.0f, 70.0f, {0.1f, 0.1f, 0.1f}, {0.1f, 0.1f, 0.1f}, {0.5f, 0.6f, 0.46f}, {0.75f, 0.8f, 0.58f}, 0.4f, 0.8f, false},
    // Rock: grey-brown granite
    {4, 1.0f, 25.0f, 70.0f, 260.0f, {0.16f, 0.15f, 0.14f}, {0.24f, 0.22f, 0.19f}, {}, {}, 0.5f, 0.0f, false},
    // Boulder
    {3, 1.0f, 40.0f, 130.0f, 600.0f, {0.15f, 0.145f, 0.135f}, {0.23f, 0.21f, 0.18f}, {}, {}, 0.5f, 0.0f, true},
    // Karst spire: pale limestone
    {3, 2.0f, 120.0f, 400.0f, 1500.0f, {0.27f, 0.265f, 0.245f}, {0.36f, 0.35f, 0.32f}, {0.8f, 0.9f, 0.8f}, {1.0f, 1.05f, 0.85f}, 0.45f, 0.5f, true},
    // Fallen log
    {2, 4.0f, 25.0f, 70.0f, 200.0f, {0.12f, 0.09f, 0.065f}, {0.18f, 0.13f, 0.09f}, {0.8f, 0.9f, 0.7f}, {1.0f, 1.0f, 0.8f}, 0.45f, 0.6f, false},
    // Grass (M4.3: widened hue range — grey-green moss tones to warm seed-head yellow)
    {3, 0.0f, 20.0f, 40.0f, 48.0f, {0.1f, 0.1f, 0.1f}, {0.1f, 0.1f, 0.1f}, {0.30f, 0.42f, 0.30f}, {0.62f, 0.62f, 0.38f}, 0.4f, 0.85f, false},
};

procgen::PropMesh makeKind(PropKind k, uint32_t seed, int lod) {
  switch (k) {
    case PropKind::Pine: return procgen::makeMountainPine(seed, lod);
    case PropKind::PineYoung: return procgen::makeYoungPine(seed, lod);
    case PropKind::Bamboo: return procgen::makeBambooClump(seed, lod);
    case PropKind::Fern: return procgen::makeFern(seed, lod);
    case PropKind::Rock: return procgen::makeRock(seed, lod);
    case PropKind::Boulder: return procgen::makeBoulder(seed, lod);
    case PropKind::KarstSpire: return procgen::makeKarstSpire(seed, lod);
    case PropKind::FallenLog: return procgen::makeFallenLog(seed, lod);
    case PropKind::Grass: return procgen::makeGrassClump(seed, lod);
    default: return {};
  }
}

int totalMeshes() {
  int n = 0;
  for (const KindStyle& s : kStyles) n += s.variants * PropRenderer::kLods;
  return n;
}

bgfx::TextureHandle uploadMipped(const std::vector<procgen::ImageRGBA8>& mips, uint64_t flags) {
  uint32_t total = 0;
  for (const auto& m : mips) total += static_cast<uint32_t>(m.pixels.size());
  const bgfx::Memory* mem = bgfx::alloc(total);
  uint32_t off = 0;
  for (const auto& m : mips) {
    std::memcpy(mem->data + off, m.pixels.data(), m.pixels.size());
    off += static_cast<uint32_t>(m.pixels.size());
  }
  gpuMemoryStats().textureBytes += total;
  return bgfx::createTexture2D(static_cast<uint16_t>(mips[0].width), static_cast<uint16_t>(mips[0].height),
                               mips.size() > 1, 1, bgfx::TextureFormat::RGBA8, flags, mem);
}

inline float hashPhase(Vec3 p) {
  const float h = std::sin(p.x * 12.9898f + p.z * 78.233f) * 43758.5453f;
  return h - std::floor(h);
}
}  // namespace

PropRenderer::~PropRenderer() {
  for (KindData& k : kinds_)
    for (Variant& v : k.variants)
      for (Lod& l : v.lods) {
        l.opaque.destroy();
        l.foliage.destroy();
      }
  for (bgfx::TextureHandle t : {foliageTex_, detailTex_})
    if (bgfx::isValid(t)) bgfx::destroy(t);
  for (bgfx::UniformHandle u : {uMaterial_, uTintA_, uTintB_, sFoliage_, sDetail_})
    if (bgfx::isValid(u)) bgfx::destroy(u);
}

int PropRenderer::meshCount() const {
  int n = 0;
  for (const KindData& k : kinds_) n += static_cast<int>(k.variants.size()) * kLods;
  return n;
}

float PropRenderer::initProgress() const {
  if (ready_) return 1.0f;
  int done = 0;
  for (int k = 0; k < genKind_; ++k) done += kStyles[k].variants * kLods;
  if (genKind_ < kPropKindCount) done += genVariant_ * kLods + genLod_;
  return 0.15f * texStage_ / 2.0f + 0.85f * static_cast<float>(done) / static_cast<float>(totalMeshes());
}

bool PropRenderer::initStep(ShaderLibrary& shaders, double budgetMs, int textureQuality) {
  if (ready_) return true;
  const auto t0 = Clock::now();
  auto elapsed = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
  const int texSize = textureQuality >= 2 ? 1024 : textureQuality == 1 ? 512 : 256;
  const uint64_t mipFlags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
  while (elapsed() < budgetMs && !ready_) {
    if (texStage_ == 0) {
      progOpaque_ = shaders.program("vs_prop", "fs_prop");
      progFoliage_ = shaders.program("vs_prop", "fs_foliage");
      progShadow_ = shaders.program("vs_prop_shadow", "fs_shadow");
      progShadowAlpha_ = shaders.program("vs_prop_shadow", "fs_shadow_alpha");
      uMaterial_ = bgfx::createUniform("u_material", bgfx::UniformType::Vec4);
      uTintA_ = bgfx::createUniform("u_tintA", bgfx::UniformType::Vec4);
      uTintB_ = bgfx::createUniform("u_tintB", bgfx::UniformType::Vec4);
      sFoliage_ = bgfx::createUniform("s_foliage", bgfx::UniformType::Sampler);
      sDetail_ = bgfx::createUniform("s_detail", bgfx::UniformType::Sampler);
      foliageTex_ = uploadMipped(procgen::buildMipChain(procgen::makeFoliageAtlas(texSize, 0xF011A6Eu), true, 0.45f),
                                 BGFX_SAMPLER_UVW_CLAMP);
      texStage_ = 1;
    } else if (texStage_ == 1) {
      detailTex_ = uploadMipped(procgen::buildMipChain(procgen::makeDetailNoise(std::min(texSize, 512), 0xDE7A11u), false),
                                mipFlags);
      texStage_ = 2;
    } else {
      const PropKind kind = static_cast<PropKind>(genKind_);
      KindData& kd = kinds_[genKind_];
      if (static_cast<int>(kd.variants.size()) <= genVariant_) kd.variants.resize(genVariant_ + 1);
      procgen::PropMesh pm = makeKind(kind, 0x9E3779B9u * (genVariant_ + 1) + genKind_ * 7919u, genLod_);
      Lod& l = kd.variants[genVariant_].lods[genLod_];
      if (!pm.opaque.indices.empty()) l.opaque = uploadMesh(pm.opaque);
      if (!pm.foliage.indices.empty()) l.foliage = uploadMesh(pm.foliage);
      const Vec3 ext{std::fmax(std::fabs(pm.boundsMin.x), std::fabs(pm.boundsMax.x)),
                     std::fmax(std::fabs(pm.boundsMin.y), std::fabs(pm.boundsMax.y)),
                     std::fmax(std::fabs(pm.boundsMin.z), std::fabs(pm.boundsMax.z))};
      l.radius = length(ext);
      if (++genLod_ >= kLods) {
        genLod_ = 0;
        if (++genVariant_ >= kStyles[genKind_].variants) {
          genVariant_ = 0;
          if (++genKind_ >= kPropKindCount) ready_ = true;
        }
      }
    }
  }
  return ready_;
}

void PropRenderer::gather(const World& world, Vec3 camPos, const Frustum* frustum, float distScale,
                          float grassDensity, bool shadow, Vec3 sc, float sr, int shadowLod, bool bigOnly) {
  for (auto& k : buckets_)
    for (auto& v : k)
      for (auto& b : v) b.clear();
  const int cps = world.chunksPerSide();
  const float cs = World::kChunkSize;
  const float sr2 = sr * sr;
  auto consider = [&](const PropInstance& in) {
    const int k = static_cast<int>(in.kind);
    const KindStyle& st = kStyles[k];
    const KindData& kd = kinds_[k];
    if (kd.variants.empty()) return;
    const int var = in.variant % static_cast<int>(kd.variants.size());
    const float rad = kd.variants[var].lods[0].radius * in.scale;
    int lod;
    if (shadow) {
      if (bigOnly && !st.big) return;
      const float dx = in.position.x - sc.x, dz = in.position.z - sc.z;
      if (dx * dx + dz * dz > sr2 + rad * rad + 2.0f * sr * rad) return;
      // Cascade 0 must not cast from props the camera won't draw.
      const Vec3 d = in.position - camPos;
      if (dot(d, d) > st.maxDist * st.maxDist * distScale * distScale) return;
      lod = shadowLod;
    } else {
      const Vec3 d = in.position - camPos;
      const float dist2 = dot(d, d);
      const float md = st.maxDist * distScale;
      if (dist2 > md * md) return;
      if (!frustum->sphereVisible(in.position + Vec3{0.0f, rad * 0.5f, 0.0f}, rad)) return;
      const float dist = std::sqrt(dist2);
      if (in.kind == PropKind::Grass) {
        // Thin out and shrink with distance (no visible pop line).
        const float keep = grassDensity * (1.0f - smoothstep(md * 0.55f, md, dist));
        if (hashPhase(in.position * 1.7f) > keep) return;
      }
      lod = dist < st.lod1 * distScale ? 0 : dist < st.lod2 * distScale ? 1 : 2;
    }
    Instance inst;
    float scale = in.scale;
    if (!shadow && in.kind == PropKind::Grass) {
      const float dist = length(in.position - camPos);
      scale *= 1.0f - 0.6f * smoothstep(st.maxDist * distScale * 0.5f, st.maxDist * distScale, dist);
    }
    inst.d0[0] = in.position.x;
    inst.d0[1] = in.position.y;
    inst.d0[2] = in.position.z;
    inst.d0[3] = scale;
    inst.d1[0] = std::sin(in.yaw);
    inst.d1[1] = std::cos(in.yaw);
    inst.d1[2] = in.tint;
    inst.d1[3] = hashPhase(in.position);
    buckets_[k][var][lod].push_back(inst);
  };
  for (int cz = 0; cz < cps; ++cz)
    for (int cx = 0; cx < cps; ++cx) {
      const WorldChunk* c = world.chunk(cx, cz);
      if (!c || !c->generated) continue;
      const float x0 = world.chunkOriginX(cx), z0 = world.chunkOriginZ(cz);
      // Chunk-level reject (with margin for big crowns / spires).
      const float margin = 40.0f;
      if (shadow) {
        const float dx = std::fmax(std::fmax(x0 - sc.x, sc.x - (x0 + cs)), 0.0f);
        const float dz = std::fmax(std::fmax(z0 - sc.z, sc.z - (z0 + cs)), 0.0f);
        if (dx * dx + dz * dz > (sr + margin) * (sr + margin)) continue;
      } else if (!frustum->aabbVisible({x0 - margin, -200.0f, z0 - margin}, {x0 + cs + margin, 600.0f, z0 + cs + margin})) {
        continue;
      }
      for (int k = 0; k < kPropKindCount; ++k)
        for (const PropInstance& in : c->scatter.instances[k]) consider(in);
      if (!shadow && grassDensity > 0.0f)
        for (const PropInstance& in : c->grass) consider(in);
    }
}

void PropRenderer::flush(bgfx::ViewId view, bool shadow) {
  const uint16_t stride = sizeof(Instance);
  for (int k = 0; k < kPropKindCount; ++k) {
    const KindStyle& st = kStyles[k];
    for (int v = 0; v < static_cast<int>(kinds_[k].variants.size()); ++v)
      for (int l = 0; l < kLods; ++l) {
        Bucket& b = buckets_[k][v][l];
        if (b.empty()) continue;
        const Lod& lod = kinds_[k].variants[v].lods[l];
        uint32_t count = static_cast<uint32_t>(b.size());
        const uint32_t avail = bgfx::getAvailInstanceDataBuffer(count, stride);
        if (avail < count) {
          stats_.droppedInstances += static_cast<int>(count - avail);
          count = avail;
        }
        if (count == 0) continue;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, count, stride);
        std::memcpy(idb.data, b.data(), static_cast<size_t>(count) * stride);
        const float material[4] = {st.material, st.alphaRef, st.translucency, 0.0f};
        const uint64_t depth = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        if (lod.opaque.valid()) {
          bgfx::setVertexBuffer(0, lod.opaque.vbh);
          bgfx::setIndexBuffer(lod.opaque.ibh);
          bgfx::setInstanceDataBuffer(&idb);
          if (shadow) {
            bgfx::setState(depth);
            bgfx::submit(view, progShadow_, 0, BGFX_DISCARD_INDEX_BUFFER | BGFX_DISCARD_VERTEX_STREAMS | BGFX_DISCARD_STATE);
            ++stats_.shadowDrawCalls;
          } else {
            const float ta[4] = {st.tintA.x, st.tintA.y, st.tintA.z, 0.0f};
            const float tb[4] = {st.tintB.x, st.tintB.y, st.tintB.z, 0.0f};
            bgfx::setUniform(uMaterial_, material);
            bgfx::setUniform(uTintA_, ta);
            bgfx::setUniform(uTintB_, tb);
            bgfx::setTexture(3, sDetail_, detailTex_);
            bindSceneShadowMap();
            bgfx::setState(BGFX_STATE_WRITE_RGB | depth | BGFX_STATE_CULL_CCW);
            bgfx::submit(view, progOpaque_, 0, BGFX_DISCARD_INDEX_BUFFER | BGFX_DISCARD_VERTEX_STREAMS | BGFX_DISCARD_STATE);
            ++stats_.drawCalls;
            stats_.triangles += count * (lod.opaque.indexCount / 3);
          }
        }
        if (lod.foliage.valid()) {
          bgfx::setVertexBuffer(0, lod.foliage.vbh);
          bgfx::setIndexBuffer(lod.foliage.ibh);
          bgfx::setInstanceDataBuffer(&idb);
          bgfx::setTexture(0, sFoliage_, foliageTex_);
          if (shadow) {
            bgfx::setState(depth);
            bgfx::submit(view, progShadowAlpha_);
            ++stats_.shadowDrawCalls;
          } else {
            const float ta[4] = {st.leafA.x, st.leafA.y, st.leafA.z, 0.0f};
            const float tb[4] = {st.leafB.x, st.leafB.y, st.leafB.z, 0.0f};
            bgfx::setUniform(uMaterial_, material);
            bgfx::setUniform(uTintA_, ta);
            bgfx::setUniform(uTintB_, tb);
            bindSceneShadowMap();
            bgfx::setState(BGFX_STATE_WRITE_RGB | depth);
            bgfx::submit(view, progFoliage_);
            ++stats_.drawCalls;
            stats_.triangles += count * (lod.foliage.indexCount / 3);
          }
        } else {
          bgfx::discard();
        }
        if (shadow) stats_.shadowInstances += static_cast<int>(count);
        else stats_.instances += static_cast<int>(count);
      }
  }
}

void PropRenderer::submitScene(bgfx::ViewId view, const World& world, Vec3 camPos, const Frustum& frustum,
                               float distScale, float grassDensity) {
  if (!ready_) return;
  gather(world, camPos, &frustum, distScale, grassDensity, false, {}, 0.0f, 0, false);
  flush(view, false);
}

void PropRenderer::submitShadow(bgfx::ViewId view, const World& world, Vec3 center, float radius, int lod,
                                bool bigOnly) {
  if (!ready_) return;
  gather(world, center, nullptr, 1.0f, 0.0f, true, center, radius, lod, bigOnly);
  flush(view, true);
}

}  // namespace aaa
