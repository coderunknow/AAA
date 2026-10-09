#include "render/skin_renderer.h"

#include "core/log.h"
#include "render/gpu_mesh.h"
#include "render/shader_library.h"

namespace aaa {

SkinRenderer::~SkinRenderer() { destroy(); }

void SkinRenderer::init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail,
                        int maxJoints) {
  maxJoints_ = maxJoints;
  program_ = shaders.program("vs_skin", "fs_skin");
  shadowProgram_ = shaders.program("vs_skin_shadow", "fs_shadow");
  uJoints_ = bgfx::createUniform("u_joints", bgfx::UniformType::Mat4, static_cast<uint16_t>(maxJoints_));
  sDetail_ = detailSampler;
  detail_ = detail;
  if (!bgfx::isValid(program_) || !bgfx::isValid(shadowProgram_))
    error_ = "skinned shader program missing or failed to link";
  AAA_LOG_INFO("skin renderer: %d joints max, program %s, shadow %s", maxJoints_,
               bgfx::isValid(program_) ? "ok" : "MISSING",
               bgfx::isValid(shadowProgram_) ? "ok" : "MISSING");
}

int SkinRenderer::upload(const SkinnedMesh& mesh) {
  Gpu gpu;
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    meshes_.push_back(gpu);
    return static_cast<int>(meshes_.size()) - 1;
  }
  // 48-byte vertex: position, normal, uv, albedo+roughness, weave+emissive,
  // joint indices, joint weights (both unorm8 vec4, so the shader scales them).
  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Uint8, true)
      .end();
  static_assert(sizeof(SkinVertex) == 48, "vertex layout must match SkinVertex");
  gpu.vbh = bgfx::createVertexBuffer(
      bgfx::copy(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size() * sizeof(SkinVertex))), layout);
  gpu.ibh = bgfx::createIndexBuffer(
      bgfx::copy(mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t))),
      BGFX_BUFFER_INDEX32);
  gpu.indices = static_cast<uint32_t>(mesh.indices.size());
  if (!gpu.valid()) {
    error_ = "skinned mesh upload failed";
    AAA_LOG_ERROR("%s (%u verts)", error_, static_cast<unsigned>(mesh.vertices.size()));
  } else {
    gpuMemoryStats().meshBytes += mesh.vertices.size() * sizeof(SkinVertex) + mesh.indices.size() * sizeof(uint32_t);
  }
  meshes_.push_back(gpu);
  return static_cast<int>(meshes_.size()) - 1;
}

void SkinRenderer::submit(bgfx::ViewId view, int meshId, const std::vector<Mat4>& palette, bool shadow) {
  if (meshId < 0 || meshId >= static_cast<int>(meshes_.size())) return;
  const Gpu& mesh = meshes_[static_cast<size_t>(meshId)];
  if (!mesh.valid() || palette.empty()) return;
  const bgfx::ProgramHandle prog = shadow ? shadowProgram_ : program_;
  if (!bgfx::isValid(prog)) return;
  const int n = palette.size() < static_cast<size_t>(maxJoints_) ? static_cast<int>(palette.size()) : maxJoints_;
  bgfx::setUniform(uJoints_, palette[0].m, static_cast<uint16_t>(n));
  bgfx::setVertexBuffer(0, mesh.vbh);
  bgfx::setIndexBuffer(mesh.ibh);
  if (shadow) {
    bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW);
  } else {
    if (bgfx::isValid(detail_)) bgfx::setTexture(3, sDetail_, detail_);
    bindSceneShadowMap();
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW);
  }
  bgfx::submit(view, prog);
}

void SkinRenderer::destroy() {
  for (Gpu& g : meshes_) {
    if (bgfx::isValid(g.vbh)) bgfx::destroy(g.vbh);
    if (bgfx::isValid(g.ibh)) bgfx::destroy(g.ibh);
    g.vbh = BGFX_INVALID_HANDLE;
    g.ibh = BGFX_INVALID_HANDLE;
  }
  meshes_.clear();
  if (bgfx::isValid(uJoints_)) bgfx::destroy(uJoints_);
  uJoints_ = BGFX_INVALID_HANDLE;
}

}  // namespace aaa
