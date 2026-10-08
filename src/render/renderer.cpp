#include "render/renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/log.h"
#include "game/game.h"
#include "render/character_renderer.h"
#include "render/frustum.h"
#include "render/gpu_mesh.h"
#include "render/prop_renderer.h"
#include "render/shader_library.h"
#include "render/terrain_renderer.h"
#include "render/water_renderer.h"
#include "render/object_renderer.h"
#include "game/wolf_pose.h"

namespace aaa {
namespace {
enum : bgfx::ViewId { kViewShadow0 = 0, kViewShadow1 = 1, kViewScene = 2, kViewPost = 3, kViewUi = 4 };
constexpr float kNear = 0.15f, kFar = 2400.0f;

bx::Vec3 bv(Vec3 v) { return {v.x, v.y, v.z}; }
}  // namespace

const char* qualityName(QualityPreset q) {
  switch (q) {
    case QualityPreset::Low: return "Low";
    case QualityPreset::Medium: return "Medium";
    default: return "High";
  }
}

RenderSettings RenderSettings::preset(QualityPreset q) {
  RenderSettings s;
  switch (q) {
    case QualityPreset::Low:
      s.renderScale = 0.67f; s.shadowSize = 1024; s.shadowSplit = 22.0f; s.shadowFar = 110.0f;
      s.propDistance = 0.6f; s.grassDensity = 0.35f; s.terrainLod = 0.6f; s.textureQuality = 0;
      break;
    case QualityPreset::Medium:
      s.renderScale = 0.85f; s.shadowSize = 1536; s.shadowSplit = 26.0f; s.shadowFar = 140.0f;
      s.propDistance = 0.8f; s.grassDensity = 0.65f; s.terrainLod = 0.8f; s.textureQuality = 1;
      break;
    case QualityPreset::High:
      break;  // defaults (target: integrated laptop GPU class, e.g. Iris Xe / M1)
  }
  return s;
}

Renderer::Renderer() = default;
Renderer::~Renderer() { shutdown(); }

namespace {
// Maps a --renderer name to a bgfx backend. Returns Count for "" (auto).
bgfx::RendererType::Enum parseRendererName(const std::string& s) {
  if (s.empty() || s == "auto") return bgfx::RendererType::Count;
  if (s == "noop") return bgfx::RendererType::Noop;
  if (s == "d3d11" || s == "direct3d11") return bgfx::RendererType::Direct3D11;
  if (s == "d3d12" || s == "direct3d12") return bgfx::RendererType::Direct3D12;
  if (s == "metal") return bgfx::RendererType::Metal;
  if (s == "vulkan") return bgfx::RendererType::Vulkan;
  if (s == "opengl" || s == "gl") return bgfx::RendererType::OpenGL;
  if (s == "opengles" || s == "gles" || s == "webgl") return bgfx::RendererType::OpenGLES;
  return bgfx::RendererType::Count;
}
}  // namespace

bool Renderer::init(const RendererInit& in) {
  init_ = in;
  quality_ = in.quality;
  settings_ = RenderSettings::preset(quality_);
  // Deterministic single-threaded rendering: calling renderFrame() before init()
  // latches bgfx onto the API thread (no internal render thread), so frame
  // submission order is fully deterministic. Required by the release (PROMPT §8.3).
  bgfx::renderFrame();
  // Backend selection (PROMPT §8.2): an explicit --renderer wins; otherwise the
  // platform preference chain is tried in order until bgfx::init succeeds.
  std::vector<bgfx::RendererType::Enum> chain;
  const bgfx::RendererType::Enum requested = parseRendererName(in.requestedRenderer);
  if (in.noop) {
    chain.push_back(bgfx::RendererType::Noop);
  } else if (requested != bgfx::RendererType::Count) {
    chain.push_back(requested);
#if defined(__EMSCRIPTEN__)
    chain.push_back(bgfx::RendererType::OpenGLES);  // WebGL2 fallback
#else
#if defined(_WIN32)
    chain.push_back(bgfx::RendererType::Vulkan);
    chain.push_back(bgfx::RendererType::OpenGL);
#elif defined(__APPLE__)
    chain.push_back(bgfx::RendererType::Metal);
#else
    chain.push_back(bgfx::RendererType::OpenGL);
    chain.push_back(bgfx::RendererType::Vulkan);
#endif
#endif
  } else {
#if defined(__EMSCRIPTEN__)
    chain.push_back(bgfx::RendererType::OpenGLES);  // WebGL2 (the web backend; never WebGPU)
#elif defined(_WIN32)
    chain.push_back(bgfx::RendererType::Direct3D11);
    chain.push_back(bgfx::RendererType::Vulkan);
    chain.push_back(bgfx::RendererType::OpenGL);
#elif defined(__APPLE__)
    chain.push_back(bgfx::RendererType::Metal);
#else
    chain.push_back(bgfx::RendererType::OpenGL);
    chain.push_back(bgfx::RendererType::Vulkan);
#endif
  }
  bgfx::RendererType::Enum selected = bgfx::RendererType::Count;
  for (size_t i = 0; i < chain.size(); ++i) {
    bgfx::Init bi;
    bi.type = chain[i];
    // This bgfx revision describes the main window as a SwapChain (nwh == NULL -> headless).
    bi.swapChain.nwh = in.nativeWindow;
    bi.swapChain.ndt = in.nativeDisplay;
    bi.swapChain.width = in.width;
    bi.swapChain.height = in.height;
    bi.reset = settings_.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    bi.limits.maxTransientVbSize = 8u << 20;
    bi.limits.maxTransientIbSize = 2u << 20;
    if (bgfx::init(bi)) {
      selected = chain[i];
      break;
    }
    AAA_LOG_WARN("bgfx::init failed for backend %s%s", bgfx::getRendererName(chain[i]),
                 i + 1 < chain.size() ? ", trying the next candidate" : "");
    bgfx::shutdown();
  }
  if (selected == bgfx::RendererType::Count) {
    AAA_LOG_ERROR("bgfx::init failed for every candidate backend");
    return false;
  }
  AAA_LOG_INFO("renderer backend: %s%s", bgfx::getRendererName(selected),
               in.requestedRenderer.empty() ? " (platform default)" : " (requested)");
  initialised_ = true;
  width_ = in.width;
  height_ = in.height;
  const bgfx::Caps* caps = bgfx::getCaps();
  info_.backend = bgfx::getRendererName(caps->rendererType);
  // Instancing and depth-compare sampling are baseline requirements in this bgfx revision
  // (no caps bits); WebGL2 provides both.
  info_.instancing = true;
  info_.shadowSampler = (caps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
  info_.maxTextureSize = caps->limits.maxTextureSize;
  info_.hdrTarget = (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
  char gpu[64];
  std::snprintf(gpu, sizeof(gpu), "vendor 0x%04x device 0x%04x", caps->vendorId, caps->deviceId);
  info_.gpu = gpu;
  AAA_LOG_INFO("renderer: %s, instancing %d, shadow compare %d, RGBA16F target %d, max texture %u",
               info_.backend, info_.instancing, info_.shadowSampler, info_.hdrTarget, info_.maxTextureSize);
  if (!info_.shadowSampler) settings_.shadows = false;

  shaders_ = std::make_unique<ShaderLibrary>(in.assetRoot);
  // Fail clearly when the compiled shader profile for the active backend is missing
  // (PROMPT §8.1: no silent fallback to another profile).
  if (!shaders_->profileDir()) {
    AAA_LOG_ERROR("no compiled shader profile for backend %s — this backend is not supported by the build",
                  info_.backend);
    shaders_.reset();
    bgfx::shutdown();
    initialised_ = false;
    return false;
  }
  if (!bgfx::isValid(shaders_->program("vs_fullscreen", "fs_sky"))) {
    AAA_LOG_ERROR("shader binaries for profile '%s' not found under '%s' — rebuild the shader assets",
                  shaders_->profileDir(), in.assetRoot.c_str());
    shaders_.reset();
    bgfx::shutdown();
    initialised_ = false;
    return false;
  }
  // The in-engine UI (SDF font atlas + program) initialises up-front so the loading
  // screen works on native (no DOM) as well as web.
  if (!ui_.init(*shaders_, in.assetRoot)) {
    AAA_LOG_ERROR("in-engine UI initialisation failed (fonts under '%s')", in.assetRoot.c_str());
    shaders_.reset();
    bgfx::shutdown();
    initialised_ = false;
    return false;
  }
  terrain_ = std::make_unique<TerrainRenderer>();
  props_ = std::make_unique<PropRenderer>();
  character_ = std::make_unique<CharacterRenderer>();
  water_ = std::make_unique<WaterRenderer>();
  objects_ = std::make_unique<ObjectRenderer>();
  fire_ = std::make_unique<FireRenderer>();

  auto U = [](const char* n, bgfx::UniformType::Enum t = bgfx::UniformType::Vec4, uint16_t num = 1) {
    return bgfx::createUniform(n, t, num);
  };
  u_.sunDir = U("u_sunDir");
  u_.sunColor = U("u_sunColor");
  u_.skyAmbient = U("u_skyAmbient");
  u_.groundAmbient = U("u_groundAmbient");
  u_.fogColor = U("u_fogColor");
  u_.fogParams = U("u_fogParams");
  u_.camPos = U("u_camPos");
  u_.wind = U("u_wind");
  u_.shadowParams = U("u_shadowParams");
  u_.shadowMtx = U("u_shadowMtx", bgfx::UniformType::Mat4, 2);
  u_.skyZenith = U("u_skyZenith");
  u_.skyHorizon = U("u_skyHorizon");
  u_.fireLight = U("u_fireLight");
  u_.fireColor = U("u_fireColor");
  u_.invViewProjSky = U("u_invViewProjSky", bgfx::UniformType::Mat4);
  u_.screenParams = U("u_screenParams");
  u_.post = U("u_post");
  u_.grade = U("u_grade");
  u_.sHdr = U("s_hdr", bgfx::UniformType::Sampler);
  u_.sShadow = U("s_shadowMap", bgfx::UniformType::Sampler);

  // Oversized triangle covering the screen.
  const float tri[9] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
  fullscreenVb_ = bgfx::createVertexBuffer(bgfx::copy(tri, sizeof(tri)), terrainVertexLayout());

  for (bgfx::ViewId v = 0; v <= kViewUi; ++v) bgfx::setViewMode(v, bgfx::ViewMode::Sequential);
  bgfx::setViewName(kViewShadow0, "shadow near");
  bgfx::setViewName(kViewShadow1, "shadow far");
  bgfx::setViewName(kViewScene, "scene");
  bgfx::setViewName(kViewPost, "post");
  bgfx::setViewName(kViewUi, "ui");
  ui_.resize(width_, height_);
  createTargets();
  return true;
}

void Renderer::shutdown() {
  if (!initialised_) return;
  water_.reset();
  objects_.reset();
  fire_.reset();
  character_.reset();
  props_.reset();
  terrain_.reset();
  ui_.shutdown();
  destroyTargets();
  if (bgfx::isValid(fullscreenVb_)) bgfx::destroy(fullscreenVb_);
  for (bgfx::UniformHandle h : {u_.sunDir, u_.sunColor, u_.skyAmbient, u_.groundAmbient, u_.fogColor, u_.fogParams,
                                u_.camPos, u_.wind, u_.shadowParams, u_.shadowMtx, u_.skyZenith, u_.skyHorizon,
                                u_.invViewProjSky, u_.screenParams, u_.post, u_.grade, u_.sHdr, u_.sShadow})
    if (bgfx::isValid(h)) bgfx::destroy(h);
  shaders_.reset();
  bgfx::shutdown();
  initialised_ = false;
}

void Renderer::destroyTargets() {
  if (bgfx::isValid(hdrFb_)) bgfx::destroy(hdrFb_);
  if (bgfx::isValid(shadowFb_)) bgfx::destroy(shadowFb_);
  hdrFb_ = BGFX_INVALID_HANDLE;
  shadowFb_ = BGFX_INVALID_HANDLE;
  hdrColor_ = BGFX_INVALID_HANDLE;
  shadowTex_ = BGFX_INVALID_HANDLE;
}

void Renderer::createTargets() {
  destroyTargets();
  sceneW_ = std::max(1u, static_cast<uint32_t>(width_ * settings_.renderScale));
  sceneH_ = std::max(1u, static_cast<uint32_t>(height_ * settings_.renderScale));
  const uint64_t rtFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP;
  const bgfx::TextureFormat::Enum colorFmt = info_.hdrTarget ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
  bgfx::TextureHandle hdr[2] = {
      bgfx::createTexture2D(static_cast<uint16_t>(sceneW_), static_cast<uint16_t>(sceneH_), false, 1, colorFmt, rtFlags),
      bgfx::createTexture2D(static_cast<uint16_t>(sceneW_), static_cast<uint16_t>(sceneH_), false, 1,
                            bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY)};
  hdrColor_ = hdr[0];
  hdrFb_ = bgfx::createFrameBuffer(2, hdr, true);
  if (settings_.shadows) {
    shadowTex_ = bgfx::createTexture2D(static_cast<uint16_t>(settings_.shadowSize * 2),
                                       static_cast<uint16_t>(settings_.shadowSize), false, 1, bgfx::TextureFormat::D16,
                                       BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_UVW_CLAMP);
    shadowFb_ = bgfx::createFrameBuffer(1, &shadowTex_, true);
  }
}

void Renderer::resize(uint32_t w, uint32_t h) {
  if (!initialised_ || w == 0 || h == 0 || (w == width_ && h == height_)) return;
  width_ = w;
  height_ = h;
  ui_.resize(w, h);
  if (init_.nativeWindow) {
    bgfx::SwapChain sc;
    sc.width = w;
    sc.height = h;
    bgfx::reset(settings_.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE, &sc);
  }
  createTargets();
}

void Renderer::setQuality(QualityPreset q) {
  if (q == quality_) return;
  quality_ = q;
  const bool hadShadows = info_.shadowSampler;
  settings_ = RenderSettings::preset(q);
  settings_.shadows = settings_.shadows && hadShadows;
  createTargets();  // texture quality only applies at the next load
}

float Renderer::loadProgress() const {
  const float t = terrain_ ? terrain_->initProgress() : 0.0f;
  const float p = props_ ? props_->initProgress() : 0.0f;
  return std::min(1.0f, 0.4f * t + 0.6f * p);
}

const char* Renderer::loadStage() const {
  switch (loadStage_) {
    case 0: return "Compiling materials";
    case 1: return "Baking terrain textures";
    case 2: return "Growing vegetation meshes";
    default: return "Ready";
  }
}

bool Renderer::loadStep(const Game& game, double budgetMs) {
  if (!initialised_) return false;
  switch (loadStage_) {
    case 0:
      skyProg_ = shaders_->program("vs_fullscreen", "fs_sky");
      tonemapProg_ = shaders_->program("vs_fullscreen", "fs_tonemap");
      loadStage_ = 1;
      return false;
    case 1:
      if (terrain_->initStep(game.world(), *shaders_, budgetMs)) loadStage_ = 2;
      return false;
    case 2:
      if (props_->initStep(*shaders_, budgetMs, settings_.textureQuality)) {
        terrain_->bindDetail(props_->detailSampler(), props_->detailTexture());
        character_->init(*shaders_, props_->detailSampler(), props_->detailTexture());
        water_->init(game.world(), *shaders_, props_->detailSampler(), props_->detailTexture());
        objects_->init(*shaders_, props_->detailSampler(), props_->detailTexture(), game.world());
        fire_->init(*shaders_, props_->detailSampler(), props_->detailTexture());
        loadStage_ = 3;
        AAA_LOG_INFO("renderer ready: %d prop meshes, %d shaders, GPU memory ~%.1f MB meshes, %.1f MB textures",
                     props_->meshCount(), shaders_->loadedShaderCount(),
                     gpuMemoryStats().meshBytes / 1048576.0, gpuMemoryStats().textureBytes / 1048576.0);
      }
      return false;
    default:
      return true;
  }
}

void Renderer::setFrameUniforms(const Game& game, const float* shadowMtx) {
  const CameraView& cv = game.camera().view();
  const float sunDir[4] = {atm_.lightDir.x, atm_.lightDir.y, atm_.lightDir.z, atm_.daylight};
  const float sunCol[4] = {atm_.sunColor.x, atm_.sunColor.y, atm_.sunColor.z, 0.0f};
  const float skyA[4] = {atm_.skyAmbient.x, atm_.skyAmbient.y, atm_.skyAmbient.z, 0.0f};
  const float grA[4] = {atm_.groundAmbient.x, atm_.groundAmbient.y, atm_.groundAmbient.z, 0.0f};
  const float fogC[4] = {atm_.fogColor.x, atm_.fogColor.y, atm_.fogColor.z, atm_.fogDensity};
  const float fogP[4] = {atm_.fogFalloff, atm_.mistBaseHeight, atm_.sunInscatter, atm_.valleyMist};
  const float cam[4] = {cv.eye.x, cv.eye.y, cv.eye.z, time_};
  // Slowly veering wind with gusts.
  const float wa = 0.6f + 0.25f * std::sin(time_ * 0.013f);
  const float gust = 0.5f + 0.5f * std::sin(time_ * 0.31f) * std::sin(time_ * 0.17f + 1.3f);
  const float wind[4] = {std::cos(wa), std::sin(wa), 0.55f, gust};
  const float sp[4] = {1.0f / (2.0f * settings_.shadowSize), settings_.shadowSplit,
                       settings_.shadows && bgfx::isValid(shadowTex_) ? 1.0f : 0.0f, settings_.shadowFar};
  const float zen[4] = {atm_.skyZenith.x, atm_.skyZenith.y, atm_.skyZenith.z, 0.0f};
  const float hor[4] = {atm_.skyHorizon.x, atm_.skyHorizon.y, atm_.skyHorizon.z, atm_.sunDisc};
  bgfx::setUniform(u_.sunDir, sunDir);
  bgfx::setUniform(u_.sunColor, sunCol);
  bgfx::setUniform(u_.skyAmbient, skyA);
  bgfx::setUniform(u_.groundAmbient, grA);
  bgfx::setUniform(u_.fogColor, fogC);
  bgfx::setUniform(u_.fogParams, fogP);
  bgfx::setUniform(u_.camPos, cam);
  bgfx::setUniform(u_.wind, wind);
  bgfx::setUniform(u_.shadowParams, sp);
  bgfx::setUniform(u_.shadowMtx, shadowMtx, 2);
  bgfx::setUniform(u_.skyZenith, zen);
  bgfx::setUniform(u_.skyHorizon, hor);

  // Nearest burning campfire to the camera drives the local light (flicker from layered sines).
  float fl[4] = {0.0f, -1000.0f, 0.0f, 0.0f}, fcol[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float bestD = 60.0f;
  int idx = 0;
  for (const Campfire& f : game.campfires()) {
    ++idx;
    if (!f.burning()) continue;
    const float d = length(f.pos - cv.eye);
    if (d >= bestD) continue;
    bestD = d;
    const float I = f.intensity();
    const float flick = 0.82f + 0.1f * std::sin(time_ * 11.0f + idx) + 0.08f * std::sin(time_ * 23.0f + idx * 2.3f);
    fl[0] = f.pos.x; fl[1] = f.pos.y + 0.7f; fl[2] = f.pos.z; fl[3] = 4.0f + 12.0f * I;
    const float k = 6.0f * I * flick;
    fcol[0] = 1.0f * k; fcol[1] = 0.42f * k; fcol[2] = 0.13f * k;
  }
  bgfx::setUniform(u_.fireLight, fl);
  bgfx::setUniform(u_.fireColor, fcol);
}

void Renderer::render(const Game& game, float realDt, float interpAlpha) {
  if (!initialised_) return;
  ++frame_;
  time_ = static_cast<float>(game.simTime());
  smoothDt_ += (realDt - smoothDt_) * 0.05f;
  const bgfx::Caps* caps = bgfx::getCaps();
  const bool homDepth = caps->homogeneousDepth;
  const bool originBL = caps->originBottomLeft;

  if (loadStage_ < 3 || !game.world().ready()) {
    // Still loading: present the clear colour plus the in-engine loading screen.
    bgfx::setViewFrameBuffer(kViewPost, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(kViewPost, 0, 0, static_cast<uint16_t>(width_), static_cast<uint16_t>(height_));
    bgfx::setViewClear(kViewPost, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x0b0e10ff, 1.0f, 0);
    bgfx::touch(kViewPost);
    ui_.submit(kViewUi);  // loading screen (title, progress bar, stage)
    bgfx::setDebug(BGFX_DEBUG_NONE);
    bgfx::frame();
    return;
  }

  // --- atmosphere --------------------------------------------------------------------------
  const TimeOfDay& tod = game.timeOfDay();
  const Vec3 pp = game.player().position();
  const WorldLayout& lay = game.world().layout();
  const float valleyFloor = lay.valleyFloorAt(game.world().fields().valleyParam(pp.x, pp.z));
  atm_ = evaluateAtmosphere({tod.sunDirection(), tod.hours(), valleyFloor});

  // --- camera (render-interpolated between the last two simulation states) -------------------
  const CameraView cv = game.cameraView(interpAlpha);
  float view[16], proj[16], viewProj[16];
  bx::mtxLookAt(view, bv(cv.eye), bv(cv.target), {0.0f, 1.0f, 0.0f});
  const float aspect = static_cast<float>(sceneW_) / static_cast<float>(sceneH_);
  bx::mtxProj(proj, bx::toDeg(cv.fovY), aspect, kNear, kFar, homDepth);
  bx::mtxMul(viewProj, view, proj);
  Frustum frustum;
  frustum.fromViewProj(viewProj);
  const Vec3 fwd = normalize(cv.target - cv.eye);
  const Vec3 fwdFlat = normalize(Vec3{fwd.x, 0.0f, fwd.z} + Vec3{0.0f, 0.0f, 1e-4f});

  // --- render interpolation: rebase characters onto the interpolated root --------------------
  // The simulation runs at a fixed 60 Hz; characters are rebased from their current
  // simulation root onto the interpolated root so they move smoothly at any frame rate.
  const Mat4 playerRebase = game.animator().renderRoot(interpAlpha) * game.animator().root().inverseRigid();
  auto wolfRebase = [&](const Wolf& w) {
    const Mat4 cur = Mat4::translation(w.pos) * Mat4::rotationY(w.yaw);
    const Vec3 p = lerp(w.prevPos, w.pos, interpAlpha);
    const float yaw = w.prevYaw + wrapAngle(w.yaw - w.prevYaw) * interpAlpha;
    return Mat4::translation(p) * Mat4::rotationY(yaw) * cur.inverseRigid();
  };

  // --- shadow cascades -----------------------------------------------------------------------
  float shadowMtx[32];
  for (int i = 0; i < 32; ++i) shadowMtx[i] = (i % 5 == 0) ? 1.0f : 0.0f;  // identity pair
  const bool shadowsOn = settings_.shadows && bgfx::isValid(shadowFb_) && atm_.lightDir.y > 0.02f;
  terrain_->resetStats();
  props_->resetStats();
  if (shadowsOn) {
    const Vec3 L = normalize(atm_.lightDir);
    const bx::Vec3 up = std::fabs(L.y) > 0.99f ? bx::Vec3{0.0f, 0.0f, 1.0f} : bx::Vec3{0.0f, 1.0f, 0.0f};
    float lightView[16];
    bx::mtxLookAt(lightView, bv(L), {0.0f, 0.0f, 0.0f}, up);  // rotation only; translation is ~unit
    const float ranges[2] = {settings_.shadowSplit, settings_.shadowFar};
    for (int c = 0; c < 2; ++c) {
      const float range = ranges[c];
      const Vec3 center = cv.eye + fwdFlat * (range * 0.42f);
      const float radius = range * 0.62f + (c == 0 ? 2.0f : 8.0f);
      const bx::Vec3 lc = bx::mul(bv(center), lightView);
      // Snap the cascade centre to whole texels to stop shimmering when the camera moves.
      const float texel = 2.0f * radius / static_cast<float>(settings_.shadowSize);
      const float sx = std::floor(lc.x / texel) * texel, sy = std::floor(lc.y / texel) * texel;
      float lightProj[16], lightVP[16];
      bx::mtxOrtho(lightProj, sx - radius, sx + radius, sy - radius, sy + radius, lc.z - 700.0f, lc.z + 700.0f, 0.0f,
                   homDepth);
      bx::mtxMul(lightVP, lightView, lightProj);
      const bgfx::ViewId vid = c == 0 ? kViewShadow0 : kViewShadow1;
      const uint16_t S = static_cast<uint16_t>(settings_.shadowSize);
      bgfx::setViewRect(vid, static_cast<uint16_t>(c * S), 0, S, S);
      bgfx::setViewFrameBuffer(vid, shadowFb_);
      bgfx::setViewClear(vid, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
      bgfx::setViewTransform(vid, lightView, lightProj);
      // clip -> atlas uv/depth
      const float sy2 = originBL ? 0.5f : -0.5f;
      const float zs = homDepth ? 0.5f : 1.0f, zb = homDepth ? 0.5f : 0.0f;
      const float bias[16] = {0.25f, 0.0f, 0.0f, 0.0f, 0.0f, sy2, 0.0f, 0.0f, 0.0f, 0.0f, zs, 0.0f,
                              0.25f + 0.5f * c, 0.5f, zb - 0.0002f, 1.0f};
      bx::mtxMul(&shadowMtx[16 * c], lightVP, bias);
      if (c == 0) setFrameUniforms(game, shadowMtx);  // first submit of the frame (wind/time used by casters)
      terrain_->submitShadow(vid, center, radius, c == 0 ? 0 : 2, game.world());
      props_->submitShadow(vid, game.world(), center, radius, c == 0 ? 1 : 2, c == 1);
      character_->submit(vid, game.animator().parts(), true, &playerRebase);
      if (game.phase() != GamePhase::LoadingWorld) {
        objects_->submit(vid, game, center, nullptr, true, time_);
        if (c == 0) {
          WolfPose pose;
          for (const Wolf& w : game.wildlife().wolves()) {
            if (length(w.pos - center) > radius) continue;
            buildWolfPose(w, time_, pose);
            const Mat4 rebase = wolfRebase(w);
            character_->submit(vid, pose.data(), kWolfPartCount, true, &rebase);
          }
        }
      }
    }
  } else {
    setFrameUniforms(game, shadowMtx);
  }

  // --- scene ---------------------------------------------------------------------------------
  bgfx::setViewFrameBuffer(kViewScene, hdrFb_);
  bgfx::setViewRect(kViewScene, 0, 0, static_cast<uint16_t>(sceneW_), static_cast<uint16_t>(sceneH_));
  const Vec3 fc = atm_.fogColor;
  auto pack = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
  bgfx::setViewClear(kViewScene, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     (pack(fc.x) << 24) | (pack(fc.y) << 16) | (pack(fc.z) << 8) | 0xff, 1.0f, 0);
  bgfx::setViewTransform(kViewScene, view, proj);
  setFrameUniforms(game, shadowMtx);
  setSceneShadowMap(u_.sShadow, shadowsOn ? shadowTex_ : bgfx::TextureHandle{bgfx::kInvalidHandle});
  terrain_->submitScene(kViewScene, game.world(), cv.eye, frustum, settings_.terrainLod, 0);
  props_->submitScene(kViewScene, game.world(), cv.eye, frustum, settings_.propDistance, settings_.grassDensity);
  if (game.phase() != GamePhase::LoadingWorld) character_->submit(kViewScene, game.animator().parts(), false, &playerRebase);
  if (game.phase() != GamePhase::LoadingWorld) {
    objects_->submit(kViewScene, game, cv.eye, &frustum, false, time_);
    WolfPose pose;
    for (const Wolf& w : game.wildlife().wolves()) {
      if (lengthSq(w.pos - cv.eye) > 160.0f * 160.0f || !frustum.sphereVisible(w.pos + Vec3{0, 0.5f, 0}, 1.2f)) continue;
      buildWolfPose(w, time_, pose);
      const Mat4 rebase = wolfRebase(w);
      character_->submit(kViewScene, pose.data(), kWolfPartCount, false, &rebase);
    }
  }
  water_->submit(kViewScene, frustum);  // translucent: after opaque geometry

  // Sky last (depth test against the far plane only).
  {
    float viewRot[16];
    std::copy(view, view + 16, viewRot);
    viewRot[12] = viewRot[13] = viewRot[14] = 0.0f;
    float vpRot[16], inv[16];
    bx::mtxMul(vpRot, viewRot, proj);
    bx::mtxInverse(inv, vpRot);
    const float screen[4] = {originBL ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_.invViewProjSky, inv);
    bgfx::setUniform(u_.screenParams, screen);
    bgfx::setTexture(3, props_->detailSampler(), props_->detailTexture());
    bgfx::setVertexBuffer(0, fullscreenVb_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL);
    if (bgfx::isValid(skyProg_)) bgfx::submit(kViewScene, skyProg_);
  }
  // Additive flames after the sky (they write no depth, so the sky would overwrite them).
  if (game.phase() != GamePhase::LoadingWorld) fire_->submit(kViewScene, game, cv.eye, frustum);

  // --- post: tonemap to the backbuffer -------------------------------------------------------
  bgfx::setViewFrameBuffer(kViewPost, BGFX_INVALID_HANDLE);
  bgfx::setViewRect(kViewPost, 0, 0, static_cast<uint16_t>(width_), static_cast<uint16_t>(height_));
  bgfx::setViewClear(kViewPost, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
  {
    // Rest / collapse transitions fade the exposure to black.
    const float fade = 1.0f - game.screenFade();
    const float post[4] = {atm_.exposure * fade * fade, 1.06f, 0.28f, 1.06f};
    const float grade[4] = {atm_.gradeHighlights.x, atm_.gradeHighlights.y, atm_.gradeHighlights.z, 0.12f};
    const float screen[4] = {originBL ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_.post, post);
    bgfx::setUniform(u_.grade, grade);
    bgfx::setUniform(u_.screenParams, screen);
    bgfx::setTexture(0, u_.sHdr, hdrColor_, BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setVertexBuffer(0, fullscreenVb_);
    // Write alpha too: the browser composites the WebGL canvas with its alpha channel, so an
    // RGB-only write leaves the canvas fully transparent (only the page background shows).
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    if (bgfx::isValid(tonemapProg_)) bgfx::submit(kViewPost, tonemapProg_);
    else bgfx::touch(kViewPost);
  }

  // In-engine UI (menus / HUD / toasts) on top of the tonemapped image.
  ui_.submit(kViewUi);

  drawDebug(game, realDt);
  bgfx::frame();
}

void Renderer::drawDebug(const Game& game, float realDt) {
  (void)realDt;
  bgfx::setDebug(debug_ ? BGFX_DEBUG_TEXT : BGFX_DEBUG_NONE);
  if (!debug_) return;
  bgfx::dbgTextClear();
  const bgfx::Stats* st = bgfx::getStats();
  const TerrainDrawStats& ts = terrain_->stats();
  const PropDrawStats& ps = props_->stats();
  const Vec3 p = game.player().position();
  int y = 1;
  bgfx::dbgTextPrintf(1, y++, 0x0f, "Mistpine dev  |  %s  |  %ux%u (scene %ux%u)  |  preset %s", info_.backend, width_,
                      height_, sceneW_, sceneH_, qualityName(quality_));
  bgfx::dbgTextPrintf(1, y++, 0x0f, "frame %.2f ms (%.0f fps, smoothed)  cpu %.2f ms  gpu %.2f ms", smoothDt_ * 1000.0f,
                      1.0f / std::max(smoothDt_, 1e-4f),
                      double(st->cpuTimeEnd - st->cpuTimeBegin) * 1000.0 / double(st->cpuTimerFreq),
                      double(st->gpuTimeEnd - st->gpuTimeBegin) * 1000.0 / double(std::max<int64_t>(st->gpuTimerFreq, 1)));
  bgfx::dbgTextPrintf(1, y++, 0x0f, "draws %u  tris ~%u  terrain chunks %d (+%d shadow)  props %d (+%d shadow) dropped %d",
                      st->numDraw, ts.triangles + ps.triangles, ts.chunksDrawn, ts.shadowChunks, ps.instances,
                      ps.shadowInstances, ps.droppedInstances);
  bgfx::dbgTextPrintf(1, y++, 0x0f, "chunks %d  detail %d  GPU mem ~%.1f MB mesh  %.1f MB tex", game.world().generatedChunkCount(),
                      game.world().detailChunkCount(), gpuMemoryStats().meshBytes / 1048576.0,
                      gpuMemoryStats().textureBytes / 1048576.0);
  bgfx::dbgTextPrintf(1, y++, 0x0f, "pos %.1f %.1f %.1f  time %05.2f h  daylight %.2f  exposure %.2f", p.x, p.y, p.z,
                      game.timeOfDay().hours(), atm_.daylight, atm_.exposure);
}

}  // namespace aaa
