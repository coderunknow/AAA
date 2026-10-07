#include "app/app.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "game/save_game.h"
#include "platform/web_bridge.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace aaa {
namespace {
constexpr const char* kSaveKey = "mistpine-save";
App* gApp = nullptr;  // for the HTML shell callbacks below

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
    else if (std::strcmp(a, "--new") == 0) o.newGame = true;
    else if (std::strcmp(a, "--qa") == 0) o.qaScenario = next();
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
  if (param("new") == "1") o.newGame = true;
  o.qaScenario = param("qa");
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
  gApp = this;
  // Headless smoke runs never touch the user's real save.
  storage_ = opts.headless ? static_cast<KeyValueStorage*>(&memoryStorage_) : &platformStorage();
  if (opts.newGame) storage_->remove(kSaveKey);
  if (auto text = storage_->load(kSaveKey)) {
    SaveData d;
    const SaveLoadResult r = deserializeSave(*text, GameConfig{}.worldSeed, d);
    AAA_LOG_INFO("save: %s", saveLoadResultName(r));
    if (r == SaveLoadResult::Ok || r == SaveLoadResult::Migrated) {
      game_->setPendingSave(d);
      hasSave_ = true;
    }
  }
  if (opts.startHours >= 0.0f) game_->timeOfDay().setHours(opts.startHours);
  if (!opts.headless) audio_.init();
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
        // QA scenarios and the ?hours override apply on top of a restored save.
        if (opts_.startHours >= 0.0f) game_->timeOfDay().setHours(opts_.startHours);
        if (!opts_.qaScenario.empty() && !game_->applyScenario(opts_.qaScenario))
          AAA_LOG_WARN("unknown qa scenario '%s'", opts_.qaScenario.c_str());
        if (!opts_.headless) {
          // Title / continue screen; the first click starts (and unlocks audio + pointer lock).
          game_->setPaused(true);
          web::reportStart(hasSave_);
        }
      }
      reportLoading();
      renderer_.render(*game_, dt);
      return true;
    case Stage::Playing:
      break;
  }

  // The diagnostics overlay is a development tool: only available with ?debug=1 / --debug.
  if (input.toggleDebug && opts_.debugOverlay) renderer_.setDebugOverlay(!renderer_.debugOverlay());
  if (input.pause) {
    const bool pause = game_->phase() != GamePhase::Paused;
    game_->setPaused(pause);
    platform_.setPointerLock(!pause);
    web::reportPause(pause);
    if (pause) saveNow("pause");
  }
  // Losing pointer lock (browser Esc) pauses the game, like most browser games.
  if (!opts_.headless && game_->phase() == GamePhase::Playing && !platform_.pointerLocked() && frames_ > 30 &&
      !platform_.focused()) {
    game_->setPaused(true);
    web::reportPause(true);
    saveNow("focus lost");
  }
  game_->update(dt, input);
  handleEvents();
  updateAudio();
  updateHud(dt);
  if (game_->phase() == GamePhase::Playing) {
    autosaveTimer_ += dt;
    if (autosaveTimer_ > 45.0) saveNow("autosave");
  }
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

void App::saveNow(const char* reason) {
  autosaveTimer_ = 0.0;
  if (!game_ || game_->phase() == GamePhase::LoadingWorld || stage_ != Stage::Playing) return;
  const bool ok = storage_->save(kSaveKey, serializeSave(game_->makeSave()));
  hasSave_ = hasSave_ || ok;
  AAA_LOG_INFO("save (%s): %s", reason, ok ? "ok" : "FAILED");
}

void App::resumeFromUi() {
  if (stage_ != Stage::Playing || game_->phase() != GamePhase::Paused) return;
  game_->setPaused(false);
  platform_.setPointerLock(true);  // inside the click handler: allowed by the browser
  web::reportPause(false);
}

void App::startOverFromUi() {
  storage_->remove(kSaveKey);
  hasSave_ = false;
  AAA_LOG_INFO("save cleared by the player");
}

void App::handleEvents() {
  const CameraView& cv = game_->camera().view();
  const Vec3 eye = cv.eye;
  Vec3 fwd = cv.target - eye;
  fwd.y = 0.0f;
  fwd = normalize(fwd);
  const Vec3 right{fwd.z, 0.0f, -fwd.x};
  const Vec3 player = game_->player().position();
  auto panOf = [&](Vec3 p) {
    Vec3 d = p - eye;
    d.y = 0.0f;
    const float l = length(d);
    return l < 0.5f ? 0.0f : dot(d * (1.0f / l), right) * 0.85f;
  };
  using audio::Sfx;
  for (const GameEvent& e : game_->events()) {
    const float dist = length(e.pos - player);
    const float pan = panOf(e.pos);
    switch (e.type) {
      case GameEvent::Notify: web::notify(e.text.c_str()); break;
      case GameEvent::Footstep: {
        const float g = 0.18f + 0.035f * game_->player().horizontalSpeed();
        audio_.play(e.value > 1.5f ? Sfx::StepStone : (e.value > 0.5f ? Sfx::StepWater : Sfx::StepEarth), g, pan * 0.3f);
        break;
      }
      case GameEvent::Land: audio_.play(Sfx::Land, std::min(1.0f, e.value / 10.0f), 0.0f); break;
      case GameEvent::Pickup: audio_.play(Sfx::Pickup, 0.6f, pan); web::notify(e.text.c_str()); break;
      case GameEvent::Eat: audio_.play(Sfx::Eat, 0.6f, 0.0f); web::notify(e.text.c_str()); break;
      case GameEvent::Drink: audio_.play(Sfx::Drink, 0.7f, pan); break;
      case GameEvent::FireLit: audio_.play(Sfx::FireLit, 0.8f, pan); web::notify(e.text.c_str()); break;
      case GameEvent::FireFed: audio_.play(Sfx::FireFed, 0.7f, pan); break;
      case GameEvent::FireOut:
        if (dist < 40.0f) audio_.play(Sfx::FireOut, 0.6f * (1.0f - dist / 40.0f), pan, dist);
        if (dist < 25.0f) web::notify("The fire has burned down to ash.");
        break;
      case GameEvent::Howl: audio_.play(Sfx::Howl, 0.25f + 0.6f * (1.0f - smoothstep(30.0f, 450.0f, dist)), pan, dist); break;
      case GameEvent::Growl:
        if (dist < 60.0f) audio_.play(Sfx::Growl, 0.9f * (1.0f - smoothstep(4.0f, 60.0f, dist)), pan, dist);
        break;
      case GameEvent::Bite:
        audio_.play(Sfx::Bite, 1.0f, pan);
        web::notify("Teeth in the dark. Get to a fire, or to the shrine.");
        break;
      case GameEvent::WolfFlee: break;
      case GameEvent::Rest: audio_.play(Sfx::Bell, 0.7f, 0.0f); web::notify(e.text.c_str()); break;
      case GameEvent::Collapse: audio_.play(Sfx::Collapse, 0.9f, 0.0f); web::notify(e.text.c_str()); break;
      case GameEvent::Wake: audio_.play(Sfx::Bell, 0.5f, 0.0f); web::notify(e.text.c_str()); break;
      case GameEvent::RequestSave: saveNow("checkpoint"); break;
    }
  }
  game_->events().clear();
  if (game_->prompt() != lastPrompt_) {
    lastPrompt_ = game_->prompt();
    web::reportPrompt(lastPrompt_.c_str());
  }
}

void App::updateAudio() {
  if (!audio_.active()) return;
  const Game& g = *game_;
  const Vec3 p = g.player().position();
  const float day = g.timeOfDay().daylight();
  audio::AmbienceLevels l;
  const float t = static_cast<float>(g.simTime());
  l.gust = 0.5f + 0.5f * std::sin(t * 0.31f) * std::sin(t * 0.17f + 1.3f);
  l.wind = 0.22f + 0.4f * smoothstep(30.0f, 160.0f, p.y);
  l.stream = 1.0f - smoothstep(3.0f, 55.0f, g.world().fields().stream(p.x, p.z));
  l.birds = smoothstep(0.35f, 0.8f, day) * (1.0f - 0.4f * l.stream);
  l.insects = 1.0f - smoothstep(0.08f, 0.4f, day);
  for (const Campfire& f : g.campfires())
    if (f.burning()) l.fire = std::fmax(l.fire, f.intensity() * (1.0f - smoothstep(1.5f, 22.0f, length(f.pos - p))));
  l.threat = g.wildlife().threat();
  l.master = g.phase() == GamePhase::Paused ? 0.2f : 0.85f;
  l.muffle = g.screenFade();
  audio_.setLevels(l);
}

void App::updateHud(float dt) {
  hudTimer_ += dt;
  if (hudTimer_ < 0.2f) return;
  hudTimer_ = 0.0f;
  const Game& g = *game_;
  const Vitals& v = g.survival().vitals();
  const float h = g.timeOfDay().hours();
  const char* phase = h < 5.0f ? "Night" : h < 7.0f ? "Dawn" : h < 11.0f ? "Morning" : h < 14.0f ? "Midday" :
                      h < 17.5f ? "Afternoon" : h < 19.5f ? "Dusk" : "Night";
  const Inventory& inv = g.inventory();
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "{\"hp\":%.1f,\"warm\":%.1f,\"food\":%.1f,\"water\":%.1f,\"wet\":%.2f,\"temp\":%.0f,"
                "\"day\":%d,\"phase\":\"%s\",\"clock\":\"%02d:%02d\",\"inv\":[%d,%d,%d,%d],\"heat\":%.2f,"
                "\"threat\":%.2f}",
                v.health, v.warmth, v.satiety, v.hydration, v.wetness, g.survival().feltTemperature(), g.day(), phase,
                static_cast<int>(h), static_cast<int>(std::fmod(h, 1.0f) * 60.0f), inv.get(ItemKind::Branch),
                inv.get(ItemKind::Flint), inv.get(ItemKind::Berries), inv.get(ItemKind::Mushroom), g.fireHeat(),
                g.wildlife().threat());
  web::reportHud(buf);
}

void App::shutdown() {
  saveNow("exit");
  audio_.shutdown();
  renderer_.shutdown();
  game_.reset();
  platform_.shutdown();
}

}  // namespace aaa

// --- HTML shell callbacks (title / pause screens) -----------------------------------------------
#if defined(__EMSCRIPTEN__)
extern "C" {
EMSCRIPTEN_KEEPALIVE void aaa_resume() {
  if (aaa::gApp) aaa::gApp->resumeFromUi();
}
EMSCRIPTEN_KEEPALIVE void aaa_start_over() {
  if (aaa::gApp) aaa::gApp->startOverFromUi();
}
}
#endif
