#include "render/object_renderer.h"

#include <cmath>

#include "game/game.h"
#include "render/shader_library.h"
#include "world/world.h"

namespace aaa {
namespace {
using procgen::StructMaterial;

struct Style {
  float albedo[3];
  float roughness;
  float weaveFreq, weaveStrength;
  float emissive;
};
// Indexed by StructMaterial (linear albedo).
const Style kStyles[] = {
    {{0.29f, 0.28f, 0.25f}, 0.85f, 1.6f, 0.55f, 0.0f},    // Stone: weathered limestone
    {{0.13f, 0.16f, 0.08f}, 0.92f, 1.6f, 0.6f, 0.0f},     // MossStone
    {{0.12f, 0.088f, 0.065f}, 0.8f, 3.0f, 0.45f, 0.0f},   // Wood: old cedar, grey-brown
    {{0.2f, 0.055f, 0.04f}, 0.62f, 2.2f, 0.5f, 0.0f},     // Lacquer: faded cinnabar
    {{0.065f, 0.07f, 0.075f}, 0.5f, 2.0f, 0.5f, 0.0f},    // RoofTile: dark fired clay
    {{0.33f, 0.3f, 0.25f}, 0.85f, 6.0f, 0.45f, 0.0f},     // Deadwood: bleached branches
    {{0.035f, 0.035f, 0.04f}, 0.28f, 4.0f, 0.3f, 0.0f},   // Flint
    {{0.045f, 0.085f, 0.028f}, 0.8f, 7.0f, 0.65f, 0.0f},  // Leaf
    {{0.38f, 0.025f, 0.04f}, 0.3f, 0.0f, 0.0f, 0.06f},    // Berry
    {{0.33f, 0.18f, 0.08f}, 0.6f, 8.0f, 0.3f, 0.0f},      // MushCap
    {{0.52f, 0.47f, 0.4f}, 0.8f, 0.0f, 0.0f, 0.0f},       // MushStem
    {{0.045f, 0.035f, 0.03f}, 0.9f, 6.0f, 0.5f, 0.0f},    // Charcoal
    {{1.0f, 0.32f, 0.07f}, 0.9f, 9.0f, 0.7f, 0.0f},       // Ember (emissive set per fire)
    {{0.2f, 0.19f, 0.18f}, 0.95f, 6.0f, 0.5f, 0.0f},      // Ash
};
static_assert(sizeof(kStyles) / sizeof(kStyles[0]) == procgen::kStructMaterialCount, "one style per material");
}  // namespace

ObjectRenderer::~ObjectRenderer() {
  for (Part& p : shrine_) p.mesh.destroy();
  for (auto& k : pickups_)
    for (auto& v : k)
      for (Part& p : v) p.mesh.destroy();
  for (Part& p : campfire_) p.mesh.destroy();
  for (bgfx::UniformHandle u : {uMaterial_, uTintA_})
    if (bgfx::isValid(u)) bgfx::destroy(u);
}

void ObjectRenderer::init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail,
                          const World& world) {
  program_ = shaders.program("vs_character", "fs_character");
  shadowProgram_ = shaders.program("vs_character_shadow", "fs_shadow");
  uMaterial_ = bgfx::createUniform("u_material", bgfx::UniformType::Vec4);
  uTintA_ = bgfx::createUniform("u_tintA", bgfx::UniformType::Vec4);
  sDetail_ = detailSampler;
  detail_ = detail;
  for (procgen::MaterialMesh& m : procgen::buildShrineMeshes(world.shrine(), world.heightfield()))
    shrine_.push_back({m.material, 0, uploadMesh(m.mesh), m.mesh.boundsMin, m.mesh.boundsMax});
  for (int k = 0; k < 4; ++k)
    for (int v = 0; v < 4; ++v)
      for (procgen::PickupMeshPart& p : procgen::buildPickupMesh(k, v))
        pickups_[k][v].push_back({p.material, p.part, uploadMesh(p.mesh), p.mesh.boundsMin, p.mesh.boundsMax});
  for (procgen::PickupMeshPart& p : procgen::buildCampfireMesh())
    campfire_.push_back({p.material, p.part, uploadMesh(p.mesh), p.mesh.boundsMin, p.mesh.boundsMax});
}

void ObjectRenderer::draw(bgfx::ViewId view, const GpuMesh& mesh, const Mat4& xf, StructMaterial mat, bool shadow,
                          float emissiveOverride) {
  const bgfx::ProgramHandle prog = shadow ? shadowProgram_ : program_;
  if (!mesh.valid() || !bgfx::isValid(prog)) return;
  bgfx::setTransform(xf.m);
  bgfx::setVertexBuffer(0, mesh.vbh);
  bgfx::setIndexBuffer(mesh.ibh);
  if (shadow) {
    bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
  } else {
    const Style& s = kStyles[static_cast<int>(mat)];
    const float m[4] = {s.albedo[0], s.albedo[1], s.albedo[2], s.roughness};
    const float t[4] = {s.weaveFreq, s.weaveStrength, emissiveOverride >= 0.0f ? emissiveOverride : s.emissive, 0.0f};
    bgfx::setUniform(uMaterial_, m);
    bgfx::setUniform(uTintA_, t);
    if (bgfx::isValid(detail_)) bgfx::setTexture(3, sDetail_, detail_);
    bindSceneShadowMap();
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW);
  }
  bgfx::submit(view, prog);
  ++drawCalls_;
}

void ObjectRenderer::submit(bgfx::ViewId view, const Game& game, Vec3 camPos, const Frustum* frustum, bool shadow,
                            float time) {
  if (!shadow) drawCalls_ = 0;
  const Mat4 I = Mat4::identity();
  for (const Part& p : shrine_) {
    if (frustum && !frustum->aabbVisible(p.boundsMin, p.boundsMax)) continue;
    if (length(game.world().shrine().center - camPos.xz()) > 420.0f) break;
    draw(view, p.mesh, I, p.material, shadow);
  }
  if (shadow) return;  // small props: no shadow casting (cheap, and contact AO reads fine)

  for (const Pickup& pk : game.pickups()) {
    const float d2 = lengthSq(pk.pos - camPos);
    if (d2 > 85.0f * 85.0f) continue;
    if (frustum && !frustum->sphereVisible(pk.pos + Vec3{0, 0.4f, 0}, 1.2f)) continue;
    const Mat4 xf = Mat4::translation(pk.pos) * Mat4::rotationY(pk.yaw) * Mat4::scale({pk.scale, pk.scale, pk.scale});
    for (const Part& part : pickups_[static_cast<int>(pk.kind) & 3][pk.variant & 3]) {
      if (part.part == 1 && !pk.available) continue;
      if (part.part == 0 && !pk.available && pk.kind != PickupKind::BerryBush) continue;
      draw(view, part.mesh, xf, part.material, false);
    }
  }

  for (size_t i = 0; i < game.campfires().size(); ++i) {
    const Campfire& f = game.campfires()[i];
    if (lengthSq(f.pos - camPos) > 200.0f * 200.0f) continue;
    if (frustum && !frustum->sphereVisible(f.pos, 1.0f)) continue;
    const Mat4 xf = Mat4::translation(f.pos);
    const float I0 = f.intensity();
    const float flicker = 0.8f + 0.2f * std::sin(time * 13.0f + i * 1.7f) * std::sin(time * 7.3f + i);
    for (const Part& part : campfire_) {
      if (part.part == 1 && !f.burning()) continue;
      if (part.part == 2 && f.burning()) continue;
      draw(view, part.mesh, xf, part.material, false, part.part == 1 ? (1.5f + 9.0f * I0) * flicker : -1.0f);
    }
  }
}

// --- FireRenderer -------------------------------------------------------------------------------

FireRenderer::~FireRenderer() {
  if (bgfx::isValid(vb_)) bgfx::destroy(vb_);
  if (bgfx::isValid(ib_)) bgfx::destroy(ib_);
  if (bgfx::isValid(uFire_)) bgfx::destroy(uFire_);
}

void FireRenderer::init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail) {
  program_ = shaders.program("vs_fire", "fs_fire");
  uFire_ = bgfx::createUniform("u_fireParams", bgfx::UniformType::Vec4);
  sDetail_ = detailSampler;
  detail_ = detail;
  struct V { float x, y, seed, type, k; };
  std::vector<V> verts;
  std::vector<uint16_t> idx;
  auto quad = [&](float seed, float type, float k) {
    const uint16_t b = static_cast<uint16_t>(verts.size());
    verts.push_back({-1, 0, seed, type, k});
    verts.push_back({1, 0, seed, type, k});
    verts.push_back({1, 1, seed, type, k});
    verts.push_back({-1, 1, seed, type, k});
    // Drawn without face culling (see submit), so a single winding is enough.
    idx.insert(idx.end(), {b, uint16_t(b + 1), uint16_t(b + 2), b, uint16_t(b + 2), uint16_t(b + 3)});
  };
  for (int i = 0; i < 7; ++i) quad(i / 7.0f, 0.0f, static_cast<float>(i));   // flame tongues
  for (int i = 0; i < 30; ++i) quad(i / 30.0f, 1.0f, static_cast<float>(i));  // sparks
  quad(0.5f, 2.0f, 0.0f);                                                      // ground glow / heat halo
  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
  vb_ = bgfx::createVertexBuffer(bgfx::copy(verts.data(), static_cast<uint32_t>(verts.size() * sizeof(V))), layout);
  ib_ = bgfx::createIndexBuffer(bgfx::copy(idx.data(), static_cast<uint32_t>(idx.size() * sizeof(uint16_t))));
  indexCount_ = static_cast<uint32_t>(idx.size());
}

void FireRenderer::submit(bgfx::ViewId view, const Game& game, Vec3 camPos, const Frustum& frustum) {
  if (!bgfx::isValid(program_) || !bgfx::isValid(vb_)) return;
  for (const Campfire& f : game.campfires()) {
    if (!f.burning() || lengthSq(f.pos - camPos) > 250.0f * 250.0f) continue;
    if (!frustum.sphereVisible(f.pos + Vec3{0, 1.0f, 0}, 2.5f)) continue;
    const float p[4] = {f.pos.x, f.pos.y + 0.05f, f.pos.z, f.intensity()};
    bgfx::setUniform(uFire_, p);
    bgfx::setVertexBuffer(0, vb_);
    bgfx::setIndexBuffer(ib_, 0, indexCount_);
    if (bgfx::isValid(detail_)) bgfx::setTexture(3, sDetail_, detail_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ADD);
    bgfx::submit(view, program_);
  }
}

}  // namespace aaa
