#pragma once
// Top-level renderer: owns bgfx lifetime, render targets, passes and sub-renderers.
#include <bgfx/bgfx.h>

#include <memory>
#include <string>
#include <vector>

#include "core/math.h"
#include "render/atmosphere.h"
#include "render/screenshot.h"
#include "render/ui_renderer.h"

namespace aaa {

class Game;
struct CameraView;
class ShaderLibrary;
class TerrainRenderer;
class PropRenderer;
class SkinRenderer;
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
  // Post effects (M4). Each has a Low/Medium/High behaviour and a capability fallback.
  bool bloom = true;            // downsample/filter/upsample bloom chain
  int bloomIterations = 2;      // separable blur iterations (H+V each)
  float bloomStrength = 0.55f;
  float bloomThreshold = 0.85f;
  bool shafts = true;           // half-res screen-space sun shafts
  float shaftsStrength = 0.32f;
  int shaftTaps = 48;
  bool ssao = true;             // half-res depth-based SSAO (High only)
  int ssaoSamples = 16;
  float ssaoRadius = 1.1f;
  float ssaoIntensity = 1.4f;
  bool fxaa = true;             // FXAA-lite in the tonemap pass
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
  uint32_t backbufferWidth() const { return width_; }
  uint32_t backbufferHeight() const { return height_; }
  // Per-frame timings for the benchmark (PROMPT §11): CPU submit time and, where the
  // backend exposes GPU timers, GPU time. gpuAvailable=false when there are no GPU timers.
  void frameStats(float& cpuMs, float& gpuMs, bool& gpuAvailable) const;
  // In-engine UI layer (menus, HUD, toasts): draw into it between beginFrame and render.
  UiRenderer& ui() { return ui_; }

  // QA: capture the backbuffer to a PNG file (PROMPT §9.8). The write completes
  // asynchronously a frame or two later; poll screenshotDone() / takeScreenshotDone().
  void requestScreenshot(const std::string& path);
  bool screenshotDone() const { return screenshotDone_; }
  void takeScreenshotDone() { screenshotDone_ = false; }

 private:
  void createTargets();
  void destroyTargets();
  void setFrameUniforms(const Game& game, const float* shadowMtx);
  void drawDebug(const Game& game, float realDt);
  // Post-effect chain (M4): bloom, sun shafts, SSAO — all half-resolution, all
  // optional per quality preset, all with capability fallbacks.
  void submitPostEffects(const CameraView& cv, const float* proj, const float* viewProj, bool originBL, bool homDepth);
  void submitFullscreen(bgfx::ViewId view, bgfx::FrameBufferHandle fb, uint32_t w, uint32_t h,
                        bgfx::ProgramHandle prog, bgfx::UniformHandle sampler, bgfx::TextureHandle src,
                        uint64_t samplerFlags);

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
  bool depthSampleable_ = false;  // caps: the scene depth can be sampled (SSAO)

  std::unique_ptr<ShaderLibrary> shaders_;
  std::unique_ptr<TerrainRenderer> terrain_;
  std::unique_ptr<PropRenderer> props_;
  std::unique_ptr<SkinRenderer> skinned_;
  int playerMesh_ = -1;
  std::vector<int> wolfMeshes_;
  std::vector<Mat4> playerPalette_;
  std::vector<std::vector<Mat4>> wolfPalettes_;
  std::unique_ptr<WaterRenderer> water_;
  std::unique_ptr<ObjectRenderer> objects_;
  std::unique_ptr<FireRenderer> fire_;
  UiRenderer ui_;
  ScreenshotCallback screenshotCb_;
  bool screenshotDone_ = false;

  bgfx::FrameBufferHandle hdrFb_ = BGFX_INVALID_HANDLE, shadowFb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle hdrColor_ = BGFX_INVALID_HANDLE, shadowTex_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle depthTex_ = BGFX_INVALID_HANDLE;  // sampleable scene depth (SSAO)
  // Bloom chain (half scene resolution): bright pass + ping-pong blur.
  bgfx::FrameBufferHandle bloomFb_[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  bgfx::TextureHandle bloomBright_ = BGFX_INVALID_HANDLE, bloomBlur_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  // Sun shafts (half scene resolution, radial blur of the bright pass).
  bgfx::FrameBufferHandle shaftsFb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle shaftsTex_ = BGFX_INVALID_HANDLE;
  // SSAO (half scene resolution, single channel).
  bgfx::FrameBufferHandle ssaoFb_[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  bgfx::TextureHandle ssaoRaw_ = BGFX_INVALID_HANDLE, ssaoBlur_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  uint32_t fxW_ = 0, fxH_ = 0;  // half-res effect target size
  // 1x1 black texture bound wherever a disabled effect's target would be, so a
  // sampler is never left unbound (bgfx uniforms are per-frame, so this is cheap).
  bgfx::TextureHandle dummyTex_ = BGFX_INVALID_HANDLE;
  // Deterministic SSAO hemisphere kernel; uploaded per frame (uniform state does not
  // survive bgfx::frame()).
  float ssaoKernel_[16][4] = {};
  bgfx::VertexBufferHandle fullscreenVb_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle skyProg_ = BGFX_INVALID_HANDLE, tonemapProg_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle brightProg_ = BGFX_INVALID_HANDLE, blurProg_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle shaftsProg_ = BGFX_INVALID_HANDLE, ssaoProg_ = BGFX_INVALID_HANDLE;

  struct Uniforms {
    bgfx::UniformHandle sunDir, sunColor, skyAmbient, groundAmbient, fogColor, fogParams, camPos, wind, shadowParams,
        shadowMtx, skyZenith, skyHorizon, fireLight, fireColor, invViewProjSky, screenParams, post, grade, sHdr, sShadow;
    // Post chain.
    bgfx::UniformHandle bright, blurDir, sunScreen, texel, effects, ssaoProj, ssaoInvProj, ssaoParams;
    bgfx::UniformHandle kernel;  // vec4[16]
    bgfx::UniformHandle sBloom, sShafts, sSsao, sBlur, sDepth;
  } u_{};
};

}  // namespace aaa
