#include "render/character_renderer.h"

#include "procgen/primitive_meshes.h"
#include "render/shader_library.h"

namespace aaa {
namespace {
struct MaterialStyle {
  float albedo[3];
  float roughness;
  float weaveFreq, weaveStrength;
  float emissive = 0.0f;
};
// Indexed by PartMaterial. Muted, travel-worn palette (linear albedo).
const MaterialStyle kMaterials[] = {
    {{0.42f, 0.27f, 0.19f}, 0.55f, 0.0f, 0.0f},    // Skin
    {{0.11f, 0.14f, 0.15f}, 0.85f, 90.0f, 0.18f},  // Jacket: indigo-grey padded cotton
    {{0.16f, 0.13f, 0.10f}, 0.9f, 70.0f, 0.15f},   // Trousers: undyed hemp
    {{0.12f, 0.07f, 0.04f}, 0.6f, 0.0f, 0.0f},     // Leather
    {{0.25f, 0.21f, 0.15f}, 0.9f, 55.0f, 0.25f},   // Canvas
    {{0.38f, 0.30f, 0.16f}, 0.85f, 120.0f, 0.35f}, // Straw hat
    {{0.15f, 0.135f, 0.115f}, 0.95f, 160.0f, 0.45f}, // Fur: grizzled grey-brown
    {{0.06f, 0.052f, 0.045f}, 0.95f, 160.0f, 0.4f},  // Dark fur (saddle, muzzle, legs)
    {{0.55f, 0.42f, 0.12f}, 0.3f, 0.0f, 0.0f, 2.5f},  // Eyeshine
};
static_assert(sizeof(kMaterials) / sizeof(kMaterials[0]) == 9, "one style per PartMaterial");
}  // namespace

CharacterRenderer::~CharacterRenderer() {
  for (GpuMesh& m : meshes_) m.destroy();
  for (bgfx::UniformHandle u : {uMaterial_, uTintA_})
    if (bgfx::isValid(u)) bgfx::destroy(u);
}

void CharacterRenderer::init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail) {
  meshes_[static_cast<int>(PartShape::Capsule)] = uploadMesh(procgen::makeCapsule(14, 4));
  meshes_[static_cast<int>(PartShape::Box)] = uploadMesh(procgen::makeBox());
  meshes_[static_cast<int>(PartShape::Sphere)] = uploadMesh(procgen::makeSphere(18, 12));
  meshes_[static_cast<int>(PartShape::Cone)] = uploadMesh(procgen::makeCone(24));
  program_ = shaders.program("vs_character", "fs_character");
  shadowProgram_ = shaders.program("vs_character_shadow", "fs_shadow");
  uMaterial_ = bgfx::createUniform("u_material", bgfx::UniformType::Vec4);
  uTintA_ = bgfx::createUniform("u_tintA", bgfx::UniformType::Vec4);
  sDetail_ = detailSampler;
  detail_ = detail;
}

void CharacterRenderer::submit(bgfx::ViewId view, const PartPose* parts, int count, bool shadow, const Mat4* rebase) {
  const bgfx::ProgramHandle prog = shadow ? shadowProgram_ : program_;
  if (!bgfx::isValid(prog)) return;
  for (int pi = 0; pi < count; ++pi) {
    const PartPose& p = parts[pi];
    const GpuMesh& mesh = meshes_[static_cast<int>(p.shape)];
    if (!mesh.valid()) continue;
    const Mat4 t = rebase ? (*rebase * p.transform) : p.transform;
    bgfx::setTransform(t.m);
    bgfx::setVertexBuffer(0, mesh.vbh);
    bgfx::setIndexBuffer(mesh.ibh);
    if (shadow) {
      bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    } else {
      const MaterialStyle& ms = kMaterials[static_cast<int>(p.material)];
      const float mat[4] = {ms.albedo[0], ms.albedo[1], ms.albedo[2], ms.roughness};
      const float weave[4] = {ms.weaveFreq, ms.weaveStrength, ms.emissive, 0.0f};
      bgfx::setUniform(uMaterial_, mat);
      bgfx::setUniform(uTintA_, weave);
      if (bgfx::isValid(detail_)) bgfx::setTexture(3, sDetail_, detail_);
      bindSceneShadowMap();
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW);
    }
    bgfx::submit(view, prog);
  }
}

}  // namespace aaa
