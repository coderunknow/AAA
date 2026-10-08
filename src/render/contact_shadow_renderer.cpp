#include "render/contact_shadow_renderer.h"

#include <cmath>
#include <vector>

#include "game/game.h"
#include "game/wildlife.h"
#include "render/shader_library.h"

namespace aaa {

ContactShadowRenderer::~ContactShadowRenderer() {
  if (bgfx::isValid(vb_)) bgfx::destroy(vb_);
  if (bgfx::isValid(ib_)) bgfx::destroy(ib_);
  if (bgfx::isValid(uDisc_)) bgfx::destroy(uDisc_);
  if (bgfx::isValid(uParams_)) bgfx::destroy(uParams_);
}

void ContactShadowRenderer::init(ShaderLibrary& shaders) {
  program_ = shaders.program("vs_contact_shadow", "fs_contact_shadow");
  // Named u_contact* — u_shadowParams is the shadow-map uniform shared by the scene shaders.
  uDisc_ = bgfx::createUniform("u_contactDisc", bgfx::UniformType::Vec4);
  uParams_ = bgfx::createUniform("u_contactParams", bgfx::UniformType::Vec4);
  // Unit disc in the XZ plane (triangle fan); the vertex shader scales and places it.
  constexpr int kSegments = 24;
  std::vector<Vec3> verts;
  std::vector<uint16_t> idx;
  verts.push_back(Vec3{0.0f, 0.0f, 0.0f});
  for (int i = 0; i < kSegments; ++i) {
    const float a = static_cast<float>(i) / kSegments * 2.0f * kPi;
    verts.push_back(Vec3{std::cos(a), 0.0f, std::sin(a)});
  }
  for (int i = 0; i < kSegments; ++i) {
    const uint16_t a = static_cast<uint16_t>(1 + i);
    const uint16_t b = static_cast<uint16_t>(1 + (i + 1) % kSegments);
    idx.insert(idx.end(), {0, a, b});
  }
  bgfx::VertexLayout layout;
  layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
  vb_ = bgfx::createVertexBuffer(bgfx::copy(verts.data(), static_cast<uint32_t>(verts.size() * sizeof(Vec3))), layout);
  ib_ = bgfx::createIndexBuffer(bgfx::copy(idx.data(), static_cast<uint32_t>(idx.size() * sizeof(uint16_t))));
  indexCount_ = static_cast<uint32_t>(idx.size());
}

void ContactShadowRenderer::submit(bgfx::ViewId view, const Game& game, Vec3 camPos, const Frustum& frustum) {
  if (!bgfx::isValid(program_) || !bgfx::isValid(vb_)) return;
  const float params[4] = {0.55f, 0.06f, 0.0f, 0.0f};  // x: strength, y: lift above the ground
  bgfx::setUniform(uParams_, params);
  bgfx::setVertexBuffer(0, vb_);
  bgfx::setIndexBuffer(ib_, 0, indexCount_);
  // Alpha-blended, depth-tested against the terrain, no depth write; no cull flag = no culling.
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA);
  auto disc = [&](Vec3 feet, float radius) {
    if (lengthSq(feet - camPos) > 60.0f * 60.0f) return;  // short-range by design (M4.2)
    if (!frustum.sphereVisible(feet, radius + 0.5f)) return;
    const float d[4] = {feet.x, feet.y + params[1], feet.z, radius};
    bgfx::setUniform(uDisc_, d);
    bgfx::submit(view, program_);
  };
  disc(game.player().position(), 0.5f);
  for (const Wolf& w : game.wildlife().wolves()) disc(w.pos, 0.38f);
}

}  // namespace aaa
