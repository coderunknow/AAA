#pragma once
// Draws the stand-in survivor from CharacterAnimator part poses (rigid primitives).
#include <bgfx/bgfx.h>

#include <array>

#include "game/character_animator.h"
#include "render/gpu_mesh.h"

namespace aaa {

class ShaderLibrary;

class CharacterRenderer {
 public:
  ~CharacterRenderer();
  void init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail);
  // `rebase` (optional): premultiplied transform that moves the pose from its
  // simulation root onto the render-interpolated root (fixed-step interpolation).
  void submit(bgfx::ViewId view, const PartPose* parts, int count, bool shadow, const Mat4* rebase = nullptr);
  void submit(bgfx::ViewId view, const std::array<PartPose, kBodyPartCount>& parts, bool shadow,
              const Mat4* rebase = nullptr) {
    submit(view, parts.data(), kBodyPartCount, shadow, rebase);
  }

 private:
  std::array<GpuMesh, 4> meshes_;  // indexed by PartShape
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE, shadowProgram_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uMaterial_ = BGFX_INVALID_HANDLE, uTintA_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sDetail_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
};

}  // namespace aaa
