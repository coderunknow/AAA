#pragma once
// GPU-resident meshes (bgfx static buffers) created from procgen::MeshData.
#include <bgfx/bgfx.h>

#include "core/math.h"
#include "procgen/mesh_builder.h"

namespace aaa {

struct GpuMesh {
  bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
  uint32_t indexCount = 0;
  uint32_t vertexCount = 0;
  bool valid() const { return bgfx::isValid(vbh) && bgfx::isValid(ibh); }
  void destroy();
};

const bgfx::VertexLayout& propVertexLayout();     // matches procgen::Vertex
const bgfx::VertexLayout& terrainVertexLayout();  // float3 grid position

GpuMesh uploadMesh(const procgen::MeshData& mesh);

// Running totals for the debug overlay / memory reports.
struct GpuMemoryStats {
  size_t meshBytes = 0;
  size_t textureBytes = 0;
};
GpuMemoryStats& gpuMemoryStats();

// Per-frame scene bindings shared by all scene-pass draws (bgfx bindings only last one submit).
void setSceneShadowMap(bgfx::UniformHandle sampler, bgfx::TextureHandle texture);  // invalid texture = none
void bindSceneShadowMap();  // call before each scene-pass submit

}  // namespace aaa
