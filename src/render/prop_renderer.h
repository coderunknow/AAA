#pragma once
// Instanced rendering of scattered props (trees, rocks, ground cover).
// Meshes are generated procedurally during loading (several variants x 3 LODs per kind).
#include <bgfx/bgfx.h>

#include <array>
#include <vector>

#include "core/math.h"
#include "render/frustum.h"
#include "render/gpu_mesh.h"
#include "world/scatter.h"

namespace aaa {

class World;
class ShaderLibrary;

struct PropDrawStats {
  int instances = 0;
  int shadowInstances = 0;
  int drawCalls = 0;
  int shadowDrawCalls = 0;
  uint32_t triangles = 0;
  int droppedInstances = 0;  // transient instance buffer exhausted
};

class PropRenderer {
 public:
  static constexpr int kLods = 3;
  static constexpr int kMaxVariants = 4;

  ~PropRenderer();
  bool initStep(ShaderLibrary& shaders, double budgetMs, int textureQuality);
  float initProgress() const;
  bool ready() const { return ready_; }

  // `densityScale` thins small props on low presets; `distScale` scales LOD/max distances.
  void submitScene(bgfx::ViewId view, const World& world, Vec3 camPos, const Frustum& frustum, float distScale,
                   float grassDensity);
  // Shadow casters around a cascade centre. lod = which mesh LOD to use.
  void submitShadow(bgfx::ViewId view, const World& world, Vec3 center, float radius, int lod, bool bigOnly);

  bgfx::TextureHandle detailTexture() const { return detailTex_; }
  bgfx::UniformHandle detailSampler() const { return sDetail_; }
  const PropDrawStats& stats() const { return stats_; }
  void resetStats() { stats_ = {}; }
  int meshCount() const;

 private:
  struct Lod { GpuMesh opaque, foliage; float radius = 1.0f; };
  struct Variant { std::array<Lod, kLods> lods; };
  struct KindData { std::vector<Variant> variants; };
  struct Instance { float d0[4]; float d1[4]; };
  using Bucket = std::vector<Instance>;

  void gather(const World& world, Vec3 camPos, const Frustum* frustum, float distScale, float grassDensity,
              bool shadow, Vec3 shadowCenter, float shadowRadius, int shadowLod, bool bigOnly);
  void flush(bgfx::ViewId view, bool shadow);

  std::array<KindData, kPropKindCount> kinds_;
  int genKind_ = 0, genVariant_ = 0, genLod_ = 0;
  int texStage_ = 0;
  bool ready_ = false;
  // buckets_[kind][variant][lod]
  std::array<std::array<std::array<Bucket, kLods>, kMaxVariants>, kPropKindCount> buckets_;

  bgfx::ProgramHandle progOpaque_ = BGFX_INVALID_HANDLE, progFoliage_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle progShadow_ = BGFX_INVALID_HANDLE, progShadowAlpha_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uMaterial_ = BGFX_INVALID_HANDLE, uTintA_ = BGFX_INVALID_HANDLE, uTintB_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sFoliage_ = BGFX_INVALID_HANDLE, sDetail_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle foliageTex_ = BGFX_INVALID_HANDLE, detailTex_ = BGFX_INVALID_HANDLE;
  PropDrawStats stats_;
};

}  // namespace aaa
