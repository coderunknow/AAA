#pragma once
// Application: wires platform, game simulation and renderer; drives staged loading.
#include <memory>
#include <string>

#include "app/ui.h"
#include "game/game.h"
#include "platform/audio_output.h"
#include "platform/platform.h"
#include "platform/storage.h"
#include "render/renderer.h"

namespace aaa {

struct AppOptions {
  bool headless = false;  // Noop renderer, no window (CI / sandbox smoke test)
  int maxFrames = 0;      // quit after N rendered frames (0 = run forever)
  QualityPreset quality = QualityPreset::High;
  std::string assetRoot;      // --assets <path> override (empty = discover next to the executable)
  float startHours = -1.0f;  // override time of day (screenshots / QA)
  bool debugOverlay = false;  // ?debug=1: shows the overlay and enables the F3 toggle
  bool newGame = false;       // ?new=1: discard the saved journey
  std::string qaScenario;     // ?qa=camp|shrine|wolves (visual QA only)
  std::string renderer;       // --renderer d3d11|vulkan|opengl|metal|noop ("" = platform chain)
  bool showVersion = false;   // --version: print the version and exit
  bool play = false;          // --play: skip the title screen (QA / benchmark captures)
  std::string screenshotPath;  // --screenshot <path>: capture the backbuffer to PNG (PROMPT §9.8)
  int screenshotFrame = 300;   // playing-frame index at which to capture
};

AppOptions parseOptions(int argc, char** argv);

class App {
 public:
  bool init(const AppOptions& opts);
  // Returns false when the app should exit.
  bool iterate();
  bool event(const SDL_Event& e);
  void shutdown();
  int exitCode() const { return exitCode_; }
  void resumeFromMenu();      // Begin / Continue (also resumes from pause)
  void startOverFromMenu();   // "Start a new journey" (two-click confirmed)
  void applySettings();       // push UI settings into renderer/platform/audio + persist

 private:
  enum class Stage { World, Renderer, Playing };
  void reportLoading();
  void handleEvents();
  void updateAudio();
  void updateHud(float dt);
  void saveNow(const char* reason);
  std::string resolveAssetRoot() const;

  AppOptions opts_;
  Platform platform_;
  std::unique_ptr<Game> game_;
  Renderer renderer_;
  Ui ui_;  // in-engine UI: title / pause / settings menus, HUD, prompts, toasts
  Stage stage_ = Stage::World;
  uint64_t lastTicks_ = 0;
  int frames_ = 0;
  int exitCode_ = 0;
  bool quit_ = false;
  double loadStartMs_ = 0.0;
  AudioOutput audio_;
  MemoryStorage memoryStorage_;
  KeyValueStorage* storage_ = nullptr;
  bool hasSave_ = false;
  double autosaveTimer_ = 0.0;
  float hudTimer_ = 0.0f;
  std::string lastPrompt_;
  // Fixed-step simulation at 60 Hz with render interpolation (PROMPT §8.6).
  static constexpr double kFixedDt = 1.0 / 60.0;
  double accumulator_ = 0.0;
  float interpAlpha_ = 0.0f;
  bool saveFailedNotified_ = false;  // tell the player once when persistence fails
  bool screenshotRequested_ = false;
  int screenshotTarget_ = 300;
};

}  // namespace aaa
