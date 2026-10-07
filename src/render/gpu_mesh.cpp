#include "render/gpu_mesh.h"

namespace aaa {

GpuMemoryStats& gpuMemoryStats() {
  static GpuMemoryStats s;
  return s;
}

void GpuMesh::destroy() {
  if (bgfx::isValid(vbh)) bgfx::destroy(vbh);
  if (bgfx::isValid(ibh)) bgfx::destroy(ibh);
  vbh = BGFX_INVALID_HANDLE;
  ibh = BGFX_INVALID_HANDLE;
}

const bgfx::VertexLayout& propVertexLayout() {
  static bgfx::VertexLayout layout = [] {
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return l;
  }();
  return layout;
}

const bgfx::VertexLayout& terrainVertexLayout() {
  static bgfx::VertexLayout layout = [] {
    bgfx::VertexLayout l;
    l.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    return l;
  }();
  return layout;
}

GpuMesh uploadMesh(const procgen::MeshData& mesh) {
  GpuMesh g;
  if (mesh.indices.empty()) return g;
  const uint32_t vbytes = static_cast<uint32_t>(mesh.vertices.size() * sizeof(procgen::Vertex));
  g.vbh = bgfx::createVertexBuffer(bgfx::copy(mesh.vertices.data(), vbytes), propVertexLayout());
  if (mesh.vertices.size() <= 65535) {
    std::vector<uint16_t> idx16(mesh.indices.begin(), mesh.indices.end());
    g.ibh = bgfx::createIndexBuffer(bgfx::copy(idx16.data(), static_cast<uint32_t>(idx16.size() * 2)));
    gpuMemoryStats().meshBytes += idx16.size() * 2;
  } else {
    g.ibh = bgfx::createIndexBuffer(bgfx::copy(mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size() * 4)),
                                    BGFX_BUFFER_INDEX32);
    gpuMemoryStats().meshBytes += mesh.indices.size() * 4;
  }
  gpuMemoryStats().meshBytes += vbytes;
  g.indexCount = static_cast<uint32_t>(mesh.indices.size());
  g.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
  return g;
}

namespace {
bgfx::UniformHandle g_shadowSampler = BGFX_INVALID_HANDLE;
bgfx::TextureHandle g_shadowTex = BGFX_INVALID_HANDLE;
}  // namespace

void setSceneShadowMap(bgfx::UniformHandle sampler, bgfx::TextureHandle texture) {
  g_shadowSampler = sampler;
  g_shadowTex = texture;
}

void bindSceneShadowMap() {
  if (bgfx::isValid(g_shadowTex) && bgfx::isValid(g_shadowSampler)) bgfx::setTexture(4, g_shadowSampler, g_shadowTex);
}

}  // namespace aaa
