#pragma once
// Top-level renderer: owns bgfx lifetime, render targets, passes and sub-renderers.
#include <bgfx/bgfx.h>

#include <memory>
#include <string>

#include "core/math.h"
#include "render/atmosphere.h"

namespace aaa {

class Game;
class ShaderLibrary;
class TerrainRenderer;
class PropRenderer;
class CharacterRenderer;
class WaterRenderer;
class ObjectRenderer;
class FireRenderer;

enum class QualityPreset : int { Low = 0, Medium = 1, High = 2 };
const char* qualityName(QualityPreset q);

struct RenderSettings {
  float renderScale = 1.0f;     // internal resolution scale for the HDR scene
  int shadowSize = 2048;        // per-cascade resolution (atlas is 2x wide)
  bool shadows = true;
  float shadowSplit = 30.0f;    // metres covered by the near cascade
  float shadowFar = 170.0f;     // metres covered by the far cascade
  float propDistance = 1.0f;    // scales prop LOD / cull distances
  float grassDensity = 1.0f;
  float terrainLod = 1.0f;
  int textureQuality = 2;       // 0..2 (procedural texture resolution)
  bool vsync = true;
  static RenderSettings preset(QualityPreset q);
};

struct RendererInit {
  void* nativeWindow = nullptr;   // HWND / NSWindow / X11 window / "#canvas"
  void* nativeDisplay = nullptr;  // X11 Display*
  uint32_t width = 1280, height = 720;
  bool noop = false;              // headless (no GPU)
  std::string assetRoot = "assets";
  QualityPreset quality = QualityPreset::High;
  std::string requestedRenderer;  // "" = platform preference chain; else d3d11|vulkan|opengl|metal|noop
};

struct RendererInfo {
  const char* backend = "none";
  bool hdrTarget = false;
  bool shadowSampler = false;
  bool instancing = false;
  uint16_t maxTextureSize = 0;
  std::string gpu;
};

class Renderer {
 public:
  Renderer();
  ~Renderer();
  bool init(const RendererInit& init);
  void shutdown();

  // Loading: creates GPU resources incrementally. Returns true when everything is ready.
  bool loadStep(const Game& game, double budgetMs);
  float loadProgress() const;
  const char* loadStage() const;

  void resize(uint32_t width, uint32_t height);
  // `interpAlpha` in [0,1): render-time interpolation between the last two
  // simulation states (fixed-step simulation, PROMPT §8.6).
  void render(const Game& game, float realDt, float interpAlpha = 0.0f);
  void setQuality(QualityPreset q);
  QualityPreset quality() const { return quality_; }
  void setDebugOverlay(bool on) { debug_ = on; }
  bool debugOverlay() const { return debug_; }
  const RendererInfo& info() const { return info_; }
  const AtmosphereState& atmosphere() const { return atm_; }
  uint32_t frameCount() const { return frame_; }

 private:
  void createTargets();
  void destroyTargets();
  void setFrameUniforms(const Game& game, const float* shadowMtx);
  void drawDebug(const Game& game, float realDt);

  RendererInit init_;
  RendererInfo info_;
  RenderSettings settings_;
  QualityPreset quality_ = QualityPreset::High;
  bool initialised_ = false;
  bool debug_ = false;
  uint32_t width_ = 0, height_ = 0, sceneW_ = 0, sceneH_ = 0;
  uint32_t frame_ = 0;
  int loadStage_ = 0;
  float time_ = 0.0f;
  float smoothDt_ = 1.0f / 60.0f;
  AtmosphereState atm_{};

  std::unique_ptr<ShaderLibrary> shaders_;
  std::unique_ptr<TerrainRenderer> terrain_;
  std::unique_ptr<PropRenderer> props_;
  std::unique_ptr<CharacterRenderer> character_;
  std::unique_ptr<WaterRenderer> water_;
  std::unique_ptr<ObjectRenderer> objects_;
  std::unique_ptr<FireRenderer> fire_;

  bgfx::FrameBufferHandle hdrFb_ = BGFX_INVALID_HANDLE, shadowFb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle hdrColor_ = BGFX_INVALID_HANDLE, shadowTex_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle fullscreenVb_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle skyProg_ = BGFX_INVALID_HANDLE, tonemapProg_ = BGFX_INVALID_HANDLE;

  struct Uniforms {
    bgfx::UniformHandle sunDir, sunColor, skyAmbient, groundAmbient, fogColor, fogParams, camPos, wind, shadowParams,
        shadowMtx, skyZenith, skyHorizon, fireLight, fireColor, invViewProjSky, screenParams, post, grade, sHdr, sShadow;
  } u_{};
};

}  // namespace aaa
