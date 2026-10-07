#pragma once
// Draws hand-placed structures and interactables (shrine, forage spots, campfires) with the
// per-draw-material "character" shader. Geometry is generated procedurally at load time.
#include <bgfx/bgfx.h>

#include <array>
#include <vector>

#include "core/math.h"
#include "procgen/structure_meshes.h"
#include "render/frustum.h"
#include "render/gpu_mesh.h"

namespace aaa {

class Game;
class World;
class ShaderLibrary;

class ObjectRenderer {
 public:
  ~ObjectRenderer();
  void init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail, const World& world);
  void submit(bgfx::ViewId view, const Game& game, Vec3 camPos, const Frustum* frustum, bool shadow, float time);
  int drawCalls() const { return drawCalls_; }

 private:
  struct Part {
    procgen::StructMaterial material;
    int part;
    GpuMesh mesh;
    Vec3 boundsMin, boundsMax;
  };
  void draw(bgfx::ViewId view, const GpuMesh& mesh, const Mat4& xf, procgen::StructMaterial mat, bool shadow,
            float emissiveOverride = -1.0f);

  std::vector<Part> shrine_;
  std::array<std::array<std::vector<Part>, 4>, 4> pickups_;  // [kind][variant]
  std::vector<Part> campfire_;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE, shadowProgram_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uMaterial_ = BGFX_INVALID_HANDLE, uTintA_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sDetail_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
  int drawCalls_ = 0;
};

// Additive flame + spark billboards for burning campfires (drawn after the sky).
class FireRenderer {
 public:
  ~FireRenderer();
  void init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail);
  void submit(bgfx::ViewId view, const Game& game, Vec3 camPos, const Frustum& frustum);

 private:
  bgfx::VertexBufferHandle vb_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib_ = BGFX_INVALID_HANDLE;
  uint32_t indexCount_ = 0;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uFire_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sDetail_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
};

}  // namespace aaa
