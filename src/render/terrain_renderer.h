#pragma once
// GPU terrain: one heightmap texture + shared LOD grid meshes with skirts.
#include <bgfx/bgfx.h>

#include <vector>

#include "core/math.h"
#include "render/frustum.h"

namespace aaa {

class World;
class ShaderLibrary;

struct TerrainDrawStats {
  int chunksDrawn = 0;
  int shadowChunks = 0;
  uint32_t triangles = 0;
};

class TerrainRenderer {
 public:
  static constexpr int kLodCount = 5;

  ~TerrainRenderer();
  // Incremental initialisation (baking normal/mask textures). Returns true when done.
  bool initStep(const World& world, ShaderLibrary& shaders, double budgetMs);
  float initProgress() const { return progress_; }
  bool ready() const { return ready_; }

  void submitScene(bgfx::ViewId view, const World& world, Vec3 camPos, const Frustum& frustum, float lodScale,
                   uint64_t extraState);
  void submitShadow(bgfx::ViewId view, Vec3 center, float radius, int lodBias, const World& world);
  void bindDetail(bgfx::UniformHandle sampler, bgfx::TextureHandle detail) { detailSampler_ = sampler; detail_ = detail; }
  const TerrainDrawStats& stats() const { return stats_; }
  void resetStats() { stats_ = {}; }

 private:
  struct Grid { bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE; bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE; uint32_t indices = 0; };
  void buildGrids();
  void setCommon(const World& world, int cx, int cz, int lod);

  int stage_ = 0;
  int row_ = 0;
  float progress_ = 0.0f;
  bool ready_ = false;
  std::vector<uint8_t> normalPixels_, maskPixels_;
  std::vector<float> chunkMinY_, chunkMaxY_;
  Grid grids_[kLodCount];
  bgfx::TextureHandle heightTex_ = BGFX_INVALID_HANDLE, normalTex_ = BGFX_INVALID_HANDLE, maskTex_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sHeight_ = BGFX_INVALID_HANDLE, sNormal_ = BGFX_INVALID_HANDLE, sMask_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle detailSampler_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uChunk_ = BGFX_INVALID_HANDLE, uInfo_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE, shadowProgram_ = BGFX_INVALID_HANDLE;
  int maskRes_ = 0;
  TerrainDrawStats stats_;
};

}  // namespace aaa
