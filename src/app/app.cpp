#include "app/app.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "platform/web_bridge.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace aaa {
namespace {
double nowMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

#if defined(__EMSCRIPTEN__)
// Query-string options: ?quality=low|medium|high&hours=7.5&debug=1
EM_JS(char*, aaa_query_string, (), {
  const s = window.location.search || "";
  const n = lengthBytesUTF8(s) + 1;
  const p = _malloc(n);
  stringToUTF8(s, p, n);
  return p;
});
#endif

QualityPreset parseQuality(const char* v) {
  if (std::strcmp(v, "low") == 0) return QualityPreset::Low;
  if (std::strcmp(v, "medium") == 0) return QualityPreset::Medium;
  return QualityPreset::High;
}
}  // namespace

AppOptions parseOptions(int argc, char** argv) {
  AppOptions o;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (std::strcmp(a, "--headless") == 0) o.headless = true;
    else if (std::strcmp(a, "--frames") == 0) o.maxFrames = std::atoi(next());
    else if (std::strcmp(a, "--quality") == 0) o.quality = parseQuality(next());
    else if (std::strcmp(a, "--assets") == 0) o.assetRoot = next();
    else if (std::strcmp(a, "--hours") == 0) o.startHours = static_cast<float>(std::atof(next()));
    else if (std::strcmp(a, "--debug") == 0) o.debugOverlay = true;
  }
#if defined(__EMSCRIPTEN__)
  char* q = aaa_query_string();
  const std::string qs(q);
  std::free(q);
  auto param = [&](const char* key) -> std::string {
    const std::string k = std::string(key) + "=";
    size_t p = qs.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    return qs.substr(p, qs.find('&', p) - p);
  };
  if (!param("quality").empty()) o.quality = parseQuality(param("quality").c_str());
  if (!param("hours").empty()) o.startHours = static_cast<float>(std::atof(param("hours").c_str()));
  if (param("debug") == "1") o.debugOverlay = true;
  if (!param("frames").empty()) o.maxFrames = std::atoi(param("frames").c_str());
  o.assetRoot = "/assets";
#else
  if (o.assetRoot.empty()) {
#ifdef AAA_ASSET_DIR
    o.assetRoot = AAA_ASSET_DIR;
#else
    o.assetRoot = "assets";
#endif
  }
#endif
  return o;
}

bool App::init(const AppOptions& opts) {
  opts_ = opts;
  loadStartMs_ = nowMs();
  AAA_LOG_INFO("Mistpine starting (%s, quality %s, assets '%s')", opts.headless ? "headless" : "windowed",
               qualityName(opts.quality), opts.assetRoot.c_str());
  if (!platform_.init("Mistpine", 1280, 720, opts.headless)) {
    web::reportError("Could not create the game window.");
    return false;
  }
  uint32_t w = 1280, h = 720;
  if (!opts.headless) platform_.pixelSize(w, h);
  RendererInit ri;
  const NativeHandles nh = platform_.nativeHandles();
  ri.nativeWindow = nh.window;
  ri.nativeDisplay = nh.display;
  ri.width = w;
  ri.height = h;
  ri.noop = opts.headless;
  ri.assetRoot = opts.assetRoot;
  ri.quality = opts.quality;
  if (!renderer_.init(ri)) {
    web::reportError("WebGL 2 is required but could not be initialised. Try an up-to-date Chrome, Edge or Firefox.");
    return false;
  }
  renderer_.setDebugOverlay(opts.debugOverlay);
  game_ = std::make_unique<Game>();
  if (opts.startHours >= 0.0f) game_->timeOfDay().setHours(opts.startHours);
  lastTicks_ = SDL_GetTicksNS();
  reportLoading();
  return true;
}

void App::reportLoading() {
  if (stage_ == Stage::World) {
    web::reportLoading(game_->loadStage(), 0.55f * game_->loadProgress());
  } else if (stage_ == Stage::Renderer) {
    web::reportLoading(renderer_.loadStage(), 0.55f + 0.45f * renderer_.loadProgress());
  }
}

bool App::event(const SDL_Event& e) {
  if (!platform_.handleEvent(e)) return false;
  return !quit_;
}

bool App::iterate() {
  const uint64_t t = SDL_GetTicksNS();
  float dt = static_cast<float>(static_cast<double>(t - lastTicks_) * 1e-9);
  lastTicks_ = t;
  if (opts_.headless) dt = 1.0f / 60.0f;  // deterministic simulation for smoke tests
  if (platform_.resized()) {
    uint32_t w, h;
    platform_.pixelSize(w, h);
    renderer_.resize(w, h);
  }
  InputFrame input = platform_.takeInput();

  switch (stage_) {
    case Stage::World:
      game_->update(dt, input, 14.0);
      if (game_->phase() != GamePhase::LoadingWorld) stage_ = Stage::Renderer;
      reportLoading();
      renderer_.render(*game_, dt);  // keeps the swap chain alive (overlay covers the canvas)
      return true;
    case Stage::Renderer:
      if (renderer_.loadStep(*game_, 14.0)) {
        stage_ = Stage::Playing;
        AAA_LOG_INFO("loading complete in %.1f s", (nowMs() - loadStartMs_) / 1000.0);
        web::reportReady();
        platform_.setPointerLock(true);
      }
      reportLoading();
      renderer_.render(*game_, dt);
      return true;
    case Stage::Playing:
      break;
  }

  if (input.toggleDebug) renderer_.setDebugOverlay(!renderer_.debugOverlay());
  if (input.pause) {
    const bool pause = game_->phase() != GamePhase::Paused;
    game_->setPaused(pause);
    platform_.setPointerLock(!pause);
    web::reportPause(pause);
  }
  // Losing pointer lock (browser Esc) pauses the game, like most browser games.
  if (!opts_.headless && game_->phase() == GamePhase::Playing && !platform_.pointerLocked() && frames_ > 30 &&
      !platform_.focused()) {
    game_->setPaused(true);
    web::reportPause(true);
  }
  game_->update(dt, input);
  renderer_.render(*game_, dt);
  ++frames_;
  // Milestone log (also lets automated browser QA confirm frames are being presented).
  if (frames_ == 1 || frames_ == 10 || frames_ == 100 || frames_ % 1000 == 0)
    AAA_LOG_INFO("frame %d presented (%.1f s since start, last dt %.1f ms)", frames_, (nowMs() - loadStartMs_) / 1000.0,
                 dt * 1000.0f);
  if (opts_.maxFrames > 0 && frames_ >= opts_.maxFrames) {
    AAA_LOG_INFO("reached %d frames, exiting", frames_);
    return false;
  }
  return !quit_;
}

void App::shutdown() {
  renderer_.shutdown();
  game_.reset();
  platform_.shutdown();
}

}  // namespace aaa
