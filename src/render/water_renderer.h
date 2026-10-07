#pragma once
// Stream water: a ribbon mesh following the authored stream polyline.
#include <bgfx/bgfx.h>

#include <vector>

#include "core/math.h"
#include "render/frustum.h"

namespace aaa {

class World;
class ShaderLibrary;

class WaterRenderer {
 public:
  ~WaterRenderer();
  void init(const World& world, ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail);
  void submit(bgfx::ViewId view, const Frustum& frustum);
  int segmentsDrawn() const { return drawn_; }

 private:
  struct Segment { uint32_t firstIndex, numIndices; Vec3 center; float radius; };
  std::vector<Segment> segments_;
  bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sDetail_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
  int drawn_ = 0;
};

}  // namespace aaa
