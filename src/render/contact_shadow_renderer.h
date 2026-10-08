#pragma once
// Short character contact shadows (PROMPT M4.2): soft dark discs under the skinned
// player and wolves, drawn in the scene pass on High quality. World-space blob
// shadows — no shadow map, no extra render pass.
#include <bgfx/bgfx.h>

#include "core/math.h"
#include "render/frustum.h"

namespace aaa {

class Game;
class ShaderLibrary;

class ContactShadowRenderer {
 public:
  ~ContactShadowRenderer();
  void init(ShaderLibrary& shaders);
  // Draws one soft disc per character (player + wolves) near the camera.
  void submit(bgfx::ViewId view, const Game& game, Vec3 camPos, const Frustum& frustum);

 private:
  bgfx::VertexBufferHandle vb_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib_ = BGFX_INVALID_HANDLE;
  uint32_t indexCount_ = 0;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uDisc_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
};

}  // namespace aaa
