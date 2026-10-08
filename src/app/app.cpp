#include "app/app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/log.h"
#include "core/version.h"
#include "game/save_game.h"
#include "platform/web_bridge.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace aaa {
namespace {
constexpr const char* kSaveKey = "mistpine-save";

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
    else if (std::strcmp(a, "--renderer") == 0) o.renderer = next();
    else if (std::strcmp(a, "--version") == 0) o.showVersion = true;
    else if (std::strcmp(a, "--play") == 0) o.play = true;
    else if (std::strcmp(a, "--screenshot") == 0) o.screenshotPath = next();
    else if (std::strcmp(a, "--screenshot-frame") == 0) o.screenshotFrame = std::atoi(next());
    else if (std::strcmp(a, "--bench") == 0) {
      o.bench = true;
      // Optional output path — only when the next token is not another flag.
      if (i + 1 < argc && argv[i + 1][0] != '-') o.benchPath = argv[++i];
    }
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
  if (param("play") == "1") o.play = true;
  o.qaScenario = param("qa");
  o.assetRoot = "/assets";  // Emscripten preloaded data bundle mount point
#endif
  // Native: an empty assetRoot means "discover next to the executable" (App::init),
  // so packaged builds never depend on the build directory or the current CWD.
  return o;
}

std::string App::resolveAssetRoot() const {
  if (!opts_.assetRoot.empty()) return opts_.assetRoot;  // explicit --assets override
#if defined(__EMSCRIPTEN__)
  return "/assets";
#else
  // Discover assets relative to the executable, never the current working
  // directory (PROMPT §8.4). Candidate layouts:
  //   <exe>/assets                 portable zip / tar / AppImage payload
  //   <exe>/../assets              build tree: build/<preset>/src/app/mistpine
  //   <exe>/../../assets           build tree from a deeper output dir
  //   <exe>/../Resources/assets    macOS bundle: Mistpine.app/Contents/MacOS/mistpine
  //   <exe>/../../Resources/assets macOS bundle from a nested output dir
  std::string base;
  if (const char* p = SDL_GetBasePath()) base = p;  // cached pointer, not owned
  if (base.empty()) {
    AAA_LOG_WARN("SDL_GetBasePath failed (%s); falling back to CWD-relative 'assets'", SDL_GetError());
    return "assets";
  }
  auto hasShaders = [](const std::string& dir) {
    // The compiled shader tree is the one asset every build must contain.
    std::string probe = dir + "/shaders";
    return std::filesystem::is_directory(probe);
  };
  std::string exeDir = base;
  if (auto pos = exeDir.find_last_of("/\\"); pos != std::string::npos) exeDir = exeDir.substr(0, pos + 1);
  std::string up1 = exeDir, up2 = exeDir;
  if (auto pos = up1.find_last_of("/\\", up1.size() - 2); pos != std::string::npos) up1 = up1.substr(0, pos + 1);
  if (auto pos = up2.find_last_of("/\\", up2.size() - 2); pos != std::string::npos) {
    up2 = up2.substr(0, pos + 1);
    if (auto pos2 = up2.find_last_of("/\\", up2.size() - 2); pos2 != std::string::npos) up2 = up2.substr(0, pos2 + 1);
  }
  const std::string candidates[] = {
      exeDir + "assets/",        up1 + "assets/",        up2 + "assets/",
      up1 + "Resources/assets/", up2 + "Resources/assets/",
  };
  for (const std::string& c : candidates) {
    if (hasShaders(c)) {
      AAA_LOG_INFO("assets: %s (discovered next to the executable)", c.c_str());
      return c;
    }
  }
  AAA_LOG_WARN("no 'assets/shaders' found next to the executable (searched %s); trying '%s' anyway",
               exeDir.c_str(), (exeDir + "assets/").c_str());
  return exeDir + "assets/";
#endif
}

bool App::init(const AppOptions& opts) {
  opts_ = opts;
  loadStartMs_ = nowMs();
  AAA_LOG_INFO("Mistpine %s starting (%s, quality %s)", appVersion(), opts.headless ? "headless" : "windowed",
               qualityName(opts.quality));
  if (!platform_.init("Mistpine", 1280, 720, opts.headless)) {
    web::reportError("Could not create the game window.");
    return false;
  }
  const std::string assetRoot = resolveAssetRoot();
  uint32_t w = 1280, h = 720;
  if (!opts.headless) platform_.pixelSize(w, h);
  RendererInit ri;
  const NativeHandles nh = platform_.nativeHandles();
  ri.nativeWindow = nh.window;
  ri.nativeDisplay = nh.display;
  ri.width = w;
  ri.height = h;
  ri.noop = opts.headless;
  ri.assetRoot = assetRoot;
  ri.quality = opts.quality;
  ri.requestedRenderer = opts_.renderer;
  if (!renderer_.init(ri)) {
    web::reportError("WebGL 2 is required but could not be initialised. Try an up-to-date Chrome, Edge or Firefox.");
    return false;
  }
  renderer_.setDebugOverlay(opts.debugOverlay);
  ui_.setRenderer(&renderer_.ui());
#if defined(__EMSCRIPTEN__)
  ui_.setNative(false);
#else
  ui_.setNative(true);
#endif
  game_ = std::make_unique<Game>();
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
      // Persisted settings apply immediately (quality, input, volume, fullscreen).
      const QualityPreset q = static_cast<QualityPreset>(std::clamp(d.quality, 0, 2));
      renderer_.setQuality(q);
      platform_.inputSettings().mouseSensitivity = d.mouseSensitivity;
      platform_.inputSettings().invertY = d.invertY;
      audio_.setMasterVolume(d.masterVolume);
      ui_.setSettings(d.quality, d.masterVolume, d.mouseSensitivity, d.invertY, d.fullscreen);
#if !defined(__EMSCRIPTEN__)
      if (d.fullscreen) platform_.setFullscreen(true);
#endif
    }
  }
  if (opts.startHours >= 0.0f) game_->timeOfDay().setHours(opts.startHours);
  if (!opts.headless) audio_.init();
  // Capture point for --screenshot: never later than 20 frames before a --frames exit.
  screenshotTarget_ = opts_.screenshotFrame;
  if (opts_.maxFrames > 0) screenshotTarget_ = std::min(screenshotTarget_, std::max(1, opts_.maxFrames - 20));
  lastTicks_ = SDL_GetTicksNS();
  reportLoading();
  return true;
}

void App::reportLoading() {
  float frac = 0.0f;
  const char* stage = "Starting";
  if (stage_ == Stage::World) {
    stage = game_->loadStage();
    frac = 0.55f * game_->loadProgress();
  } else if (stage_ == Stage::Renderer) {
    stage = renderer_.loadStage();
    frac = 0.55f + 0.45f * renderer_.loadProgress();
  }
  ui_.setLoading(stage, frac);
  web::reportLoading(stage, frac);
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
        if (opts_.bench) {
          // Benchmark (PROMPT §11): no menus, a campfire ready for the night phase, and
          // a deterministic camera route driven from simulation time in updateBench().
          benchActive_ = true;
          benchPhases_[0].name = "day";
          benchPhases_[0].hours = 9.0f;
          benchPhases_[1].name = "dusk";
          benchPhases_[1].hours = 18.7f;
          benchPhases_[2].name = "night campfire";
          benchPhases_[2].hours = 22.5f;
          game_->applyScenario("camp");  // burning fire in front of the spawn point
          ui_.setMenu(Ui::Menu::Playing);
          ui_.setStarted(true);
          AAA_LOG_INFO("benchmark: day -> dusk -> night campfire, %.0f s sim per phase, JSON -> %s",
                       kBenchPhaseSeconds, opts_.benchPath.c_str());
        } else if (!opts_.headless && !opts_.play) {
          // Title / continue screen (in-engine UI); the first click starts the journey.
          game_->setPaused(true);
          ui_.setMenu(Ui::Menu::Title);
          ui_.setHasSave(hasSave_);
          ui_.setStarted(false);
        } else {
          // Headless smoke runs and --play captures run straight through (no menus).
          ui_.setMenu(Ui::Menu::Playing);
          ui_.setStarted(true);
        }
      }
      reportLoading();
      renderer_.render(*game_, dt);
      return true;
    case Stage::Playing:
      break;
  }

  // In-engine UI frame: size, mouse state, logic, then draw into the renderer's UI layer.
  ui_.beginFrame(renderer_.backbufferWidth(), renderer_.backbufferHeight());
  UiMouse um;
  um.pos = platform_.mousePositionPixels();
  um.click = platform_.takeMouseClick();

  // The diagnostics overlay is a development tool: only available with ?debug=1 / --debug.
  if (input.toggleDebug && opts_.debugOverlay) renderer_.setDebugOverlay(!renderer_.debugOverlay());
  if (input.pause) {
    // Esc is menu-aware: Settings -> back to Pause, Pause -> resume, Playing -> pause.
    switch (ui_.menu()) {
      case Ui::Menu::Settings:
        ui_.setMenu(Ui::Menu::Pause);
        break;
      case Ui::Menu::Pause:
        resumeFromMenu();
        break;
      case Ui::Menu::Title:
      case Ui::Menu::Loading:
        break;  // Esc does nothing on the title / loading screens
      case Ui::Menu::Playing:
        game_->setPaused(true);
        platform_.setPointerLock(false);
        ui_.setMenu(Ui::Menu::Pause);
        saveNow("pause");
        break;
    }
  }
  // Losing pointer lock (browser Esc) pauses the game, like most browser games.
  // (Disabled while benchmarking: an unattended window must never stall the run.)
  if (!opts_.headless && !benchActive_ && game_->phase() == GamePhase::Playing && !platform_.pointerLocked() &&
      frames_ > 30 && !platform_.focused()) {
    game_->setPaused(true);
    ui_.setMenu(Ui::Menu::Pause);
    saveNow("focus lost");
  }
  // Fixed-step simulation at 60 Hz with render interpolation (PROMPT §8.6).
  // The accumulator is clamped so a long hitch never causes a simulation spiral.
  if (game_->phase() == GamePhase::Paused) {
    // Menus keep the world alive in real time; the simulation clock does not advance.
    accumulator_ = 0.0;
    interpAlpha_ = 0.0f;
    game_->update(dt, InputFrame{});
  } else {
    accumulator_ += dt;
    if (accumulator_ > 0.25) accumulator_ = 0.25;  // hitch clamp
    InputFrame simInput = input;
    while (accumulator_ >= kFixedDt) {
      game_->update(static_cast<float>(kFixedDt), simInput);
      accumulator_ -= kFixedDt;
      // Edge-triggered actions are consumed by the first sub-step of the frame.
      simInput.crouchToggle = simInput.jump = simInput.interact = simInput.buildFire = simInput.eat = false;
    }
    interpAlpha_ = static_cast<float>(accumulator_ / kFixedDt);
  }
  handleEvents();
  updateAudio();
  updateHud(dt);
  if (game_->phase() == GamePhase::Playing) {
    autosaveTimer_ += dt;
    if (autosaveTimer_ > 45.0) saveNow("autosave");
  }
  // UI logic (menu transitions, hit-testing, settings changes).
  UiActions actions;
  ui_.update(dt, *game_, um, actions);
  if (actions.startOrResume) resumeFromMenu();
  if (actions.startOver) startOverFromMenu();
  if (actions.settingsChanged) applySettings();
  // Draw the UI into the renderer's UI layer, then render (scene + UI + debug).
  renderer_.ui().begin();
  ui_.draw(*game_);
  renderer_.render(*game_, dt, interpAlpha_);
  ++frames_;
  // Benchmark: collect this frame's wall/CPU/GPU timings into the current phase.
  if (benchActive_ && !benchDone_) {
    const int phase = std::min(2, static_cast<int>(game_->simTime() / kBenchPhaseSeconds));
    float cpuMs = 0.0f, gpuMs = 0.0f;
    bool gpuAvail = false;
    renderer_.frameStats(cpuMs, gpuMs, gpuAvail);
    BenchPhase& bp = benchPhases_[phase];
    bp.wallMs.push_back(dt * 1000.0f);
    bp.cpuMs.push_back(cpuMs);
    if (gpuAvail) bp.gpuMs.push_back(gpuMs);
    if (game_->simTime() >= 3.0 * kBenchPhaseSeconds) {
      writeBenchJson(true);
      benchDone_ = true;
      AAA_LOG_INFO("benchmark complete, exiting");
      return false;
    }
  }
  // QA screenshot capture (PROMPT §9.8): request once at the target frame, then
  // exit when bgfx has written the PNG (the callback fires a frame or two later).
  if (!opts_.screenshotPath.empty() && !screenshotRequested_ && frames_ >= screenshotTarget_) {
    renderer_.requestScreenshot(opts_.screenshotPath);
    screenshotRequested_ = true;
  }
  if (screenshotRequested_ && renderer_.screenshotDone()) {
    AAA_LOG_INFO("screenshot written, exiting");
    return false;
  }
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

namespace {
struct BenchStat {
  float min = 0.0f, avg = 0.0f, p95 = 0.0f, max = 0.0f;
};
BenchStat summarize(std::vector<float> v) {
  BenchStat s;
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  s.min = v.front();
  s.max = v.back();
  double sum = 0.0;
  for (const float x : v) sum += x;
  s.avg = static_cast<float>(sum / static_cast<double>(v.size()));
  s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
  return s;
}
void appendStat(std::string& out, const char* key, const BenchStat& s) {
  char buf[192];
  std::snprintf(buf, sizeof(buf), "      \"%s\": {\"min\": %.3f, \"avg\": %.3f, \"p95\": %.3f, \"max\": %.3f}", key,
                s.min, s.avg, s.p95, s.max);
  out += buf;
}
}  // namespace

void App::updateBench() {
  const double t = game_->simTime();
  const int phase = std::min(2, static_cast<int>(t / kBenchPhaseSeconds));
  if (phase != benchPhase_) {
    benchPhase_ = phase;
    game_->timeOfDay().setHours(benchPhases_[static_cast<size_t>(phase)].hours);
    AAA_LOG_INFO("bench phase %d: %s (%.1f h)", phase, benchPhases_[static_cast<size_t>(phase)].name,
                 benchPhases_[static_cast<size_t>(phase)].hours);
  }
  // One full camera sweep per phase — a pure function of simulation time, so the route
  // is identical at any render frame rate.
  const double phaseT = t - static_cast<double>(phase) * kBenchPhaseSeconds;
  const float yaw = static_cast<float>(phaseT / kBenchPhaseSeconds * kTwoPi) - kPi * 0.5f;
  game_->camera().setBenchView(yaw, radians(-16.0f), 5.5f);
}

void App::writeBenchJson(bool completed) {
  const RendererInfo& ri = renderer_.info();
  std::string out;
  out += "{\n";
  out += std::string("  \"app\": \"Mistpine\",\n  \"version\": \"") + appVersion() + "\",\n";
  out += std::string("  \"renderer\": \"") + ri.backend + "\",\n";
  out += std::string("  \"gpu\": \"") + ri.gpu + "\",\n";
  out += std::string("  \"quality\": \"") + qualityName(renderer_.quality()) + "\",\n";
  char buf[256];
  std::snprintf(buf, sizeof(buf), "  \"backbuffer\": [%u, %u],\n", renderer_.backbufferWidth(),
                renderer_.backbufferHeight());
  out += buf;
  out += std::string("  \"completed\": ") + (completed ? "true" : "false") + ",\n";
  out += "  \"phases\": [\n";
  std::vector<float> allWall, allCpu, allGpu;
  bool anyGpu = false;
  for (int p = 0; p < 3; ++p) {
    const BenchPhase& bp = benchPhases_[static_cast<size_t>(p)];
    out += "    {\n";
    out += std::string("      \"name\": \"") + bp.name + "\",\n";
    std::snprintf(buf, sizeof(buf), "      \"hours\": %.1f,\n      \"frames\": %llu,\n", bp.hours,
                 static_cast<unsigned long long>(bp.wallMs.size()));
    out += buf;
    appendStat(out, "wallMs", summarize(bp.wallMs));
    out += ",\n";
    appendStat(out, "cpuMs", summarize(bp.cpuMs));
    out += ",\n";
    if (bp.gpuMs.empty()) {
      out += "      \"gpuMs\": null\n";
    } else {
      appendStat(out, "gpuMs", summarize(bp.gpuMs));
      out += "\n";
      anyGpu = true;
    }
    out += std::string("    }") + (p < 2 ? ",\n" : "\n");
    allWall.insert(allWall.end(), bp.wallMs.begin(), bp.wallMs.end());
    allCpu.insert(allCpu.end(), bp.cpuMs.begin(), bp.cpuMs.end());
    allGpu.insert(allGpu.end(), bp.gpuMs.begin(), bp.gpuMs.end());
  }
  out += "  ],\n";
  out += "  \"total\": {\n";
  std::snprintf(buf, sizeof(buf), "    \"frames\": %llu,\n", static_cast<unsigned long long>(allWall.size()));
  out += buf;
  appendStat(out, "wallMs", summarize(allWall));
  out += ",\n";
  appendStat(out, "cpuMs", summarize(allCpu));
  out += ",\n";
  if (allGpu.empty()) {
    out += "    \"gpuMs\": null\n";
  } else {
    appendStat(out, "gpuMs", summarize(allGpu));
    out += "\n";
  }
  out += "  },\n";
  out += std::string("  \"gpuTimingAvailable\": ") + (anyGpu ? "true" : "false") + "\n";
  out += "}\n";
#if defined(__EMSCRIPTEN__)
  web::reportBench(out.c_str());
  AAA_LOG_INFO("benchmark JSON (%llu bytes) exposed as window.__mistpineBench",
               static_cast<unsigned long long>(out.size()));
#else
  std::ofstream f(opts_.benchPath, std::ios::binary | std::ios::trunc);
  if (f) {
    f << out;
    AAA_LOG_INFO("benchmark JSON written to %s", opts_.benchPath.c_str());
  } else {
    AAA_LOG_ERROR("could not write benchmark JSON to %s", opts_.benchPath.c_str());
  }
#endif
  const BenchStat wall = summarize(allWall);
  const BenchStat cpu = summarize(allCpu);
  // %llu with an explicit cast rather than %zu: the MinGW toolchain links against
  // MSVCRT, whose printf does not understand the C99 'z' length modifier.
  AAA_LOG_INFO("bench summary: %llu frames, wall avg %.2f ms (p95 %.2f), cpu avg %.2f ms (p95 %.2f)%s",
               static_cast<unsigned long long>(allWall.size()), wall.avg, wall.p95, cpu.avg, cpu.p95,
               anyGpu ? ", gpu timings included" : "");
}

void App::saveNow(const char* reason) {
  autosaveTimer_ = 0.0;
  if (!game_ || game_->phase() == GamePhase::LoadingWorld || stage_ != Stage::Playing) return;
  const bool ok = storage_->save(kSaveKey, serializeSave(game_->makeSave()));
  hasSave_ = hasSave_ || ok;
  AAA_LOG_INFO("save (%s): %s", reason, ok ? "ok" : "FAILED");
  // If persistence is unavailable (quota, privacy mode, unwritable pref path), tell
  // the player once and keep running — never falsely claim the save worked (PROMPT §8.7).
  if (!ok && !saveFailedNotified_) {
    saveFailedNotified_ = true;
    ui_.notify("Your progress could not be saved (storage unavailable or full). The game keeps running, but progress will be lost when you leave.");
  }
}

void App::resumeFromMenu() {
  if (stage_ != Stage::Playing) return;
  game_->setPaused(false);
  ui_.setMenu(Ui::Menu::Playing);
  ui_.setStarted(true);
  // Pointer lock: on the web the shell also requests it from the click gesture; SDL's
  // relative mode stays armed so the next click re-locks if the browser deferred it.
  platform_.setPointerLock(true);
}

void App::startOverFromMenu() {
  storage_->remove(kSaveKey);
  hasSave_ = false;
  ui_.setHasSave(false);
  game_->restartJourney();
  ui_.setMenu(Ui::Menu::Playing);
  ui_.setStarted(true);
  platform_.setPointerLock(true);
  AAA_LOG_INFO("save cleared by the player (new journey)");
}

void App::applySettings() {
  renderer_.setQuality(static_cast<QualityPreset>(std::clamp(ui_.quality(), 0, 2)));
  platform_.inputSettings().mouseSensitivity = ui_.sensitivity();
  platform_.inputSettings().invertY = ui_.invertY();
  audio_.setMasterVolume(ui_.volume());
#if !defined(__EMSCRIPTEN__)
  platform_.setFullscreen(ui_.fullscreen());
#endif
  saveNow("settings");
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
      case GameEvent::Notify: ui_.notify(e.text.c_str()); break;
      case GameEvent::Footstep: {
        const float g = 0.18f + 0.035f * game_->player().horizontalSpeed();
        audio_.play(e.value > 1.5f ? Sfx::StepStone : (e.value > 0.5f ? Sfx::StepWater : Sfx::StepEarth), g, pan * 0.3f);
        break;
      }
      case GameEvent::Land: audio_.play(Sfx::Land, std::min(1.0f, e.value / 10.0f), 0.0f); break;
      case GameEvent::Pickup: audio_.play(Sfx::Pickup, 0.6f, pan); ui_.notify(e.text.c_str()); break;
      case GameEvent::Eat: audio_.play(Sfx::Eat, 0.6f, 0.0f); ui_.notify(e.text.c_str()); break;
      case GameEvent::Drink: audio_.play(Sfx::Drink, 0.7f, pan); break;
      case GameEvent::FireLit: audio_.play(Sfx::FireLit, 0.8f, pan); ui_.notify(e.text.c_str()); break;
      case GameEvent::FireFed: audio_.play(Sfx::FireFed, 0.7f, pan); break;
      case GameEvent::FireOut:
        if (dist < 40.0f) audio_.play(Sfx::FireOut, 0.6f * (1.0f - dist / 40.0f), pan, dist);
        if (dist < 25.0f) ui_.notify("The fire has burned down to ash.");
        break;
      case GameEvent::Howl: audio_.play(Sfx::Howl, 0.25f + 0.6f * (1.0f - smoothstep(30.0f, 450.0f, dist)), pan, dist); break;
      case GameEvent::Growl:
        if (dist < 60.0f) audio_.play(Sfx::Growl, 0.9f * (1.0f - smoothstep(4.0f, 60.0f, dist)), pan, dist);
        break;
      case GameEvent::Bite:
        audio_.play(Sfx::Bite, 1.0f, pan);
        ui_.notify("Teeth in the dark. Get to a fire, or to the shrine.");
        break;
      case GameEvent::WolfFlee: break;
      case GameEvent::Rest: audio_.play(Sfx::Bell, 0.7f, 0.0f); ui_.notify(e.text.c_str()); break;
      case GameEvent::Collapse: audio_.play(Sfx::Collapse, 0.9f, 0.0f); ui_.notify(e.text.c_str()); break;
      case GameEvent::Wake: audio_.play(Sfx::Bell, 0.5f, 0.0f); ui_.notify(e.text.c_str()); break;
      case GameEvent::RequestSave: saveNow("checkpoint"); break;
    }
  }
  game_->events().clear();
  lastPrompt_ = game_->prompt();  // the in-engine UI reads the prompt straight from the game
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
  // Read-only state snapshot for the automated browser playtest (window.__mistpineState).
  // `noLock` lists the button rects whose click must not request pointer lock on the web.
  std::string noLock;
  for (const auto& r : ui_.noLockRects()) {
    char rect[80];
    std::snprintf(rect, sizeof(rect), "%s[%.0f,%.0f,%.0f,%.0f]", noLock.empty() ? "" : ",", r.x, r.y, r.w, r.h);
    noLock += rect;
  }
  char buf[1024];
  std::snprintf(buf, sizeof(buf),
                "{\"version\":\"%s\",\"menu\":\"%s\",\"phase\":\"%s\",\"started\":%d,\"hasSave\":%d,"
                "\"hp\":%.1f,\"warm\":%.1f,\"food\":%.1f,\"water\":%.1f,\"wet\":%.2f,\"temp\":%.0f,"
                "\"day\":%d,\"clock\":\"%02d:%02d\",\"phaseName\":\"%s\",\"inv\":[%d,%d,%d,%d],\"heat\":%.2f,"
                "\"threat\":%.2f,\"noLock\":[%s]}",
                appVersion(), ui_.menuName(),
                g.phase() == GamePhase::Paused ? "paused" : g.phase() == GamePhase::Playing ? "playing" : "loading",
                ui_.started() ? 1 : 0, hasSave_ ? 1 : 0, v.health, v.warmth, v.satiety, v.hydration, v.wetness,
                g.survival().feltTemperature(), g.day(), static_cast<int>(h), static_cast<int>(std::fmod(h, 1.0f) * 60.0f),
                phase, inv.get(ItemKind::Branch), inv.get(ItemKind::Flint), inv.get(ItemKind::Berries),
                inv.get(ItemKind::Mushroom), g.fireHeat(), g.wildlife().threat(), noLock.c_str());
  web::reportState(buf);
}

void App::shutdown() {
  saveNow("exit");
  audio_.shutdown();
  renderer_.shutdown();
  game_.reset();
  platform_.shutdown();
}

}  // namespace aaa
