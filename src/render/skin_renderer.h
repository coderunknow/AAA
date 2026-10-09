#pragma once
// GPU skinned-mesh renderer (PROMPT §10.5): uploads a SkinnedMesh once and draws it
// with a joint palette uniform, in both the scene and the shadow pass.
#include <bgfx/bgfx.h>

#include <vector>

#include "game/skin.h"

namespace aaa {

class ShaderLibrary;

class SkinRenderer {
 public:
  ~SkinRenderer();
  void init(ShaderLibrary& shaders, bgfx::UniformHandle detailSampler, bgfx::TextureHandle detail,
            int maxJoints = 32);

  // Uploads (or replaces) a mesh; returns an index used by submit().
  int upload(const SkinnedMesh& mesh);
  // Draws palette-skinned geometry. `palette` must hold exactly the skeleton's joints.
  void submit(bgfx::ViewId view, int meshId, const std::vector<Mat4>& palette, bool shadow);
  void destroy();

  int meshCount() const { return static_cast<int>(meshes_.size()); }
  const char* lastError() const { return error_; }
  bool meshReady(int meshId) const {
    return meshId >= 0 && meshId < static_cast<int>(meshes_.size()) && meshes_[static_cast<size_t>(meshId)].valid();
  }

 private:
  struct Gpu {
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
    uint32_t indices = 0;
    bool valid() const { return bgfx::isValid(vbh) && bgfx::isValid(ibh); }
  };
  std::vector<Gpu> meshes_;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE, shadowProgram_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle uJoints_ = BGFX_INVALID_HANDLE, sDetail_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
  int maxJoints_ = 32;
  const char* error_ = "";
};

}  // namespace aaa
