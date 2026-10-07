#pragma once
// Application: wires platform, game simulation and renderer; drives staged loading.
#include <memory>
#include <string>

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
  std::string assetRoot;
  float startHours = -1.0f;  // override time of day (screenshots / QA)
  bool debugOverlay = false;  // ?debug=1: shows the overlay and enables the F3 toggle
  bool newGame = false;       // ?new=1: discard the saved journey
  std::string qaScenario;     // ?qa=camp|shrine|wolves (visual QA only)
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
  // Called from the HTML shell (title / pause screens).
  void resumeFromUi();
  void startOverFromUi();

 private:
  enum class Stage { World, Renderer, Playing };
  void reportLoading();
  void handleEvents();
  void updateAudio();
  void updateHud(float dt);
  void saveNow(const char* reason);

  AppOptions opts_;
  Platform platform_;
  std::unique_ptr<Game> game_;
  Renderer renderer_;
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
};

}  // namespace aaa
