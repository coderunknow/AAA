#include "render/terrain_renderer.h"

#include <chrono>
#include <cmath>

#include "core/log.h"
#include "core/noise.h"
#include "procgen/mesh_builder.h"
#include "render/gpu_mesh.h"
#include "render/shader_library.h"
#include "world/world.h"

namespace aaa {
namespace {
constexpr int kChunkQuads = 64;  // quads per chunk side at LOD 0 (1 m spacing)
using Clock = std::chrono::steady_clock;
}  // namespace

TerrainRenderer::~TerrainRenderer() {
  for (Grid& g : grids_) {
    if (bgfx::isValid(g.vbh)) bgfx::destroy(g.vbh);
    if (bgfx::isValid(g.ibh)) bgfx::destroy(g.ibh);
  }
  for (bgfx::TextureHandle t : {heightTex_, normalTex_, maskTex_})
    if (bgfx::isValid(t)) bgfx::destroy(t);
  for (bgfx::UniformHandle u : {sHeight_, sNormal_, sMask_, uChunk_, uInfo_})
    if (bgfx::isValid(u)) bgfx::destroy(u);
}

void TerrainRenderer::buildGrids() {
  for (int lod = 0; lod < kLodCount; ++lod) {
    const int n = kChunkQuads >> lod;
    const float step = static_cast<float>(1 << lod);
    std::vector<float> verts;
    auto vtx = [&](float gx, float skirt, float gz) {
      verts.insert(verts.end(), {gx * step, skirt, gz * step});
      return static_cast<uint32_t>(verts.size() / 3 - 1);
    };
    for (int z = 0; z <= n; ++z)
      for (int x = 0; x <= n; ++x) vtx(static_cast<float>(x), 0.0f, static_cast<float>(z));
    std::vector<uint16_t> idx;
    auto at = [&](int x, int z) { return static_cast<uint16_t>(z * (n + 1) + x); };
    for (int z = 0; z < n; ++z)
      for (int x = 0; x < n; ++x) {
        // Alternate diagonals for a more isotropic triangulation.
        const uint16_t a = at(x, z), b = at(x + 1, z), c = at(x + 1, z + 1), d = at(x, z + 1);
        // Front faces point up (+Y): cross(b-a, c-a) must be +Y in our convention.
        if ((x + z) & 1) idx.insert(idx.end(), {a, d, b, b, d, c});
        else idx.insert(idx.end(), {a, d, c, a, c, b});
      }
    // Skirts along the four edges (vertical strips pushed down in the vertex shader).
    auto skirtEdge = [&](int x0, int z0, int dx, int dz, bool flip) {
      for (int i = 0; i < n; ++i) {
        const int xa = x0 + dx * i, za = z0 + dz * i, xb = xa + dx, zb = za + dz;
        const uint16_t ta = at(xa, za), tb = at(xb, zb);
        const uint16_t ba = static_cast<uint16_t>(vtx(static_cast<float>(xa), 1.0f, static_cast<float>(za)));
        const uint16_t bb = static_cast<uint16_t>(vtx(static_cast<float>(xb), 1.0f, static_cast<float>(zb)));
        if (flip) idx.insert(idx.end(), {ta, tb, bb, ta, bb, ba});
        else idx.insert(idx.end(), {ta, bb, tb, ta, ba, bb});
      }
    };
    // Winding verified by hand: (ta,tb,bb) faces -Z for the z=0 edge and +X for the x=n edge.
    skirtEdge(0, 0, 1, 0, true);
    skirtEdge(0, n, 1, 0, false);
    skirtEdge(0, 0, 0, 1, false);
    skirtEdge(n, 0, 0, 1, true);
    Grid& g = grids_[lod];
    g.vbh = bgfx::createVertexBuffer(bgfx::copy(verts.data(), static_cast<uint32_t>(verts.size() * 4)), terrainVertexLayout());
    g.ibh = bgfx::createIndexBuffer(bgfx::copy(idx.data(), static_cast<uint32_t>(idx.size() * 2)));
    g.indices = static_cast<uint32_t>(idx.size());
    gpuMemoryStats().meshBytes += verts.size() * 4 + idx.size() * 2;
  }
}

bool TerrainRenderer::initStep(const World& world, ShaderLibrary& shaders, double budgetMs) {
  if (ready_) return true;
  const auto t0 = Clock::now();
  const Heightfield& hf = world.heightfield();
  const int res = hf.resolution();
  auto elapsed = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
  while (elapsed() < budgetMs && !ready_) {
    switch (stage_) {
      case 0: {  // GPU resources that need no baking
        program_ = shaders.program("vs_terrain", "fs_terrain");
        shadowProgram_ = shaders.program("vs_terrain_shadow", "fs_shadow");
        sHeight_ = bgfx::createUniform("s_heightmap", bgfx::UniformType::Sampler);
        sNormal_ = bgfx::createUniform("s_normalmap", bgfx::UniformType::Sampler);
        sMask_ = bgfx::createUniform("s_terrainMask", bgfx::UniformType::Sampler);
        uChunk_ = bgfx::createUniform("u_terrainChunk", bgfx::UniformType::Vec4);
        uInfo_ = bgfx::createUniform("u_terrainInfo", bgfx::UniformType::Vec4);
        buildGrids();
        heightTex_ = bgfx::createTexture2D(static_cast<uint16_t>(res), static_cast<uint16_t>(res), false, 1,
                                           bgfx::TextureFormat::R32F, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
                                           bgfx::copy(hf.data().data(), static_cast<uint32_t>(hf.data().size() * 4)));
        gpuMemoryStats().textureBytes += hf.data().size() * 4;
        normalPixels_.resize(static_cast<size_t>(res) * res * 4);
        maskRes_ = world.fields().res;
        maskPixels_.resize(static_cast<size_t>(maskRes_) * maskRes_ * 4);
        // Per-chunk vertical bounds for culling.
        const int cps = world.chunksPerSide();
        chunkMinY_.assign(static_cast<size_t>(cps) * cps, 1e9f);
        chunkMaxY_.assign(static_cast<size_t>(cps) * cps, -1e9f);
        for (int z = 0; z < res; ++z)
          for (int x = 0; x < res; ++x) {
            const int cx = std::min(cps - 1, x / kChunkQuads), cz = std::min(cps - 1, z / kChunkQuads);
            const float h = hf.at(x, z);
            const size_t i = static_cast<size_t>(cz) * cps + cx;
            chunkMinY_[i] = std::fmin(chunkMinY_[i], h);
            chunkMaxY_[i] = std::fmax(chunkMaxY_[i], h);
          }
        stage_ = 1;
        row_ = 0;
        break;
      }
      case 1: {  // normal map rows
        for (int x = 0; x < res; ++x) {
          const Vec3 n = hf.normal(hf.worldX(x), hf.worldZ(row_));
          uint8_t* p = &normalPixels_[(static_cast<size_t>(row_) * res + x) * 4];
          p[0] = procgen::toUnorm8(n.x * 0.5f + 0.5f);
          p[1] = procgen::toUnorm8(n.z * 0.5f + 0.5f);
          p[2] = procgen::toUnorm8(n.y);
          p[3] = 255;
        }
        if (++row_ >= res) {
          normalTex_ = bgfx::createTexture2D(static_cast<uint16_t>(res), static_cast<uint16_t>(res), false, 1,
                                             bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                             bgfx::copy(normalPixels_.data(), static_cast<uint32_t>(normalPixels_.size())));
          gpuMemoryStats().textureBytes += normalPixels_.size();
          normalPixels_.clear();
          normalPixels_.shrink_to_fit();
          stage_ = 2;
          row_ = 0;
        }
        progress_ = 0.1f + 0.4f * row_ / res;
        break;
      }
      case 2: {  // material mask rows (wetness, trail, forest litter, grass)
        const WorldFields& F = world.fields();
        ScatterContext ctx{&world.layout(), &hf, &F};
        for (int x = 0; x < maskRes_; ++x) {
          const float wx = -F.half + x * F.cell, wz = -F.half + row_ * F.cell;
          const float sd = F.stream(wx, wz);
          const float wet = 1.0f - smoothstep(2.0f, 10.0f, sd);
          const float trailN = 0.75f + 0.25f * noise::gradient2(wx * 0.3f, wz * 0.3f, 77);
          const float trail = (1.0f - smoothstep(0.6f, 2.4f, F.trail(wx, wz) * trailN)) * (1.0f - wet);
          const float litter = saturate(propDensity(ctx, PropKind::Pine, wx, wz) * 1.6f +
                                        propDensity(ctx, PropKind::PineYoung, wx, wz) * 0.8f);
          const float grass = saturate(propDensity(ctx, PropKind::Grass, wx, wz));
          uint8_t* p = &maskPixels_[(static_cast<size_t>(row_) * maskRes_ + x) * 4];
          p[0] = procgen::toUnorm8(wet);
          p[1] = procgen::toUnorm8(trail);
          p[2] = procgen::toUnorm8(litter);
          p[3] = procgen::toUnorm8(grass);
        }
        if (++row_ >= maskRes_) {
          maskTex_ = bgfx::createTexture2D(static_cast<uint16_t>(maskRes_), static_cast<uint16_t>(maskRes_), false, 1,
                                           bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                           bgfx::copy(maskPixels_.data(), static_cast<uint32_t>(maskPixels_.size())));
          gpuMemoryStats().textureBytes += maskPixels_.size();
          maskPixels_.clear();
          maskPixels_.shrink_to_fit();
          ready_ = true;
        }
        progress_ = 0.5f + 0.5f * row_ / maskRes_;
        break;
      }
    }
  }
  return ready_;
}

void TerrainRenderer::setCommon(const World& world, int cx, int cz, int lod) {
  const Heightfield& hf = world.heightfield();
  const float chunk[4] = {world.chunkOriginX(cx), world.chunkOriginZ(cz), 1.0f, 2.0f + 3.0f * lod};
  const float info[4] = {static_cast<float>(hf.resolution()), hf.halfSize(), 1.0f / hf.worldSize(), hf.cellSize()};
  bgfx::setUniform(uChunk_, chunk);
  bgfx::setUniform(uInfo_, info);
  bgfx::setTexture(0, sHeight_, heightTex_);
  bgfx::setVertexBuffer(0, grids_[lod].vbh);
  bgfx::setIndexBuffer(grids_[lod].ibh);
}

void TerrainRenderer::submitScene(bgfx::ViewId view, const World& world, Vec3 camPos, const Frustum& frustum,
                                  float lodScale, uint64_t extraState) {
  if (!ready_ || !bgfx::isValid(program_)) return;
  const int cps = world.chunksPerSide();
  const float cs = World::kChunkSize;
  for (int cz = 0; cz < cps; ++cz)
    for (int cx = 0; cx < cps; ++cx) {
      const size_t i = static_cast<size_t>(cz) * cps + cx;
      const Vec3 mn{world.chunkOriginX(cx), chunkMinY_[i] - 8.0f, world.chunkOriginZ(cz)};
      const Vec3 mx{mn.x + cs, chunkMaxY_[i] + 1.0f, mn.z + cs};
      if (!frustum.aabbVisible(mn, mx)) continue;
      const float dx = std::fmax(std::fmax(mn.x - camPos.x, camPos.x - mx.x), 0.0f);
      const float dz = std::fmax(std::fmax(mn.z - camPos.z, camPos.z - mx.z), 0.0f);
      const float dy = std::fmax(std::fmax(mn.y - camPos.y, camPos.y - mx.y), 0.0f);
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz) / lodScale;
      const int lod = d < 70.0f ? 0 : d < 160.0f ? 1 : d < 330.0f ? 2 : d < 650.0f ? 3 : 4;
      setCommon(world, cx, cz, lod);
      bgfx::setTexture(1, sNormal_, normalTex_);
      bgfx::setTexture(2, sMask_, maskTex_);
      if (bgfx::isValid(detail_)) bgfx::setTexture(3, detailSampler_, detail_);
      bindSceneShadowMap();
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW |
                     extraState);
      bgfx::submit(view, program_);
      ++stats_.chunksDrawn;
      stats_.triangles += grids_[lod].indices / 3;
    }
}

void TerrainRenderer::submitShadow(bgfx::ViewId view, Vec3 center, float radius, int lodBias, const World& world) {
  if (!ready_ || !bgfx::isValid(shadowProgram_)) return;
  const int cps = world.chunksPerSide();
  const float cs = World::kChunkSize;
  for (int cz = 0; cz < cps; ++cz)
    for (int cx = 0; cx < cps; ++cx) {
      const float x0 = world.chunkOriginX(cx), z0 = world.chunkOriginZ(cz);
      const float dx = std::fmax(std::fmax(x0 - center.x, center.x - (x0 + cs)), 0.0f);
      const float dz = std::fmax(std::fmax(z0 - center.z, center.z - (z0 + cs)), 0.0f);
      // Generous radius: mountains outside the cascade can still cast into it along the sun direction.
      if (dx * dx + dz * dz > radius * radius * 2.5f) continue;
      const int lod = std::min(kLodCount - 1, lodBias + (dx * dx + dz * dz > radius * radius ? 1 : 0));
      setCommon(world, cx, cz, lod);
      // No culling in the shadow pass: thin ridges must cast from both sides.
      bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
      bgfx::submit(view, shadowProgram_);
      ++stats_.shadowChunks;
    }
}

}  // namespace aaa
