#pragma once
// Renderer-independent game state and simulation step.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/camera.h"
#include "game/character_animator.h"
#include "game/input.h"
#include "game/interactables.h"
#include "game/player.h"
#include "game/save_game.h"
#include "game/survival.h"
#include "game/time_of_day.h"
#include "game/wildlife.h"
#include "world/world.h"

namespace aaa {

enum class GamePhase { LoadingWorld, Playing, Paused };

struct GameConfig {
  uint32_t worldSeed = 20261007u;
  int heightfieldResolution = 1025;
  float streamRadius = 300.0f;        // scatter data kept within this radius
  float streamReleaseRadius = 380.0f;
  int maxChunksPerFrame = 2;
  float detailRadius = 70.0f;         // grass
  float detailReleaseRadius = 110.0f;
};

// Things that happened this step, for audio, HUD notifications and the save system.
struct GameEvent {
  enum Type : uint8_t {
    Notify, Pickup, Eat, Drink, FireLit, FireFed, FireOut, Rest, Collapse, Wake, Howl, Growl, Bite, WolfFlee,
    Footstep, Land, RequestSave
  } type;
  Vec3 pos;
  float value = 0.0f;  // footstep: 0 earth, 1 water, 2 stone
  std::string text;
};

// A short scripted fade used for resting and collapsing.
enum class Transition : uint8_t { None, RestOut, RestIn, CollapseOut, CollapseIn };

class Game {
 public:
  explicit Game(const GameConfig& config = {});

  // Advances loading (bounded by budgetMs) or the simulation.
  void update(float dt, const InputFrame& input, double loadBudgetMs = 12.0);

  GamePhase phase() const { return phase_; }
  float loadProgress() const { return world_->loadProgress(); }
  const char* loadStage() const { return world_->loadStage(); }

  const World& world() const { return *world_; }
  World& world() { return *world_; }
  const PlayerController& player() const { return player_; }
  const ThirdPersonCamera& camera() const { return camera_; }
  ThirdPersonCamera& camera() { return camera_; }
  // Render-interpolated camera view between the last two simulation states.
  CameraView cameraView(float alpha) const { return camera_.view(alpha); }
  const CharacterAnimator& animator() const { return animator_; }
  CharacterAnimator& animator() { return animator_; }
  TimeOfDay& timeOfDay() { return time_; }
  const TimeOfDay& timeOfDay() const { return time_; }
  double simTime() const { return simTime_; }
  void setPaused(bool p);

  const Survival& survival() const { return survival_; }
  const Inventory& inventory() const { return inventory_; }
  const std::vector<Pickup>& pickups() const { return pickups_; }
  const std::vector<Campfire>& campfires() const { return campfires_; }
  const Wildlife& wildlife() const { return wildlife_; }
  int day() const { return day_; }
  const std::string& prompt() const { return prompt_; }
  // 0 = clear view, 1 = black (rest / collapse transitions).
  float screenFade() const { return fade_; }
  float fireHeat() const { return fireHeat_; }

  std::vector<GameEvent>& events() { return events_; }

  // Save integration: state is applied when the world finishes loading.
  void setPendingSave(const SaveData& d) { pendingSave_ = d; hasPendingSave_ = true; }
  SaveData makeSave() const;
  bool startedFromSave() const { return startedFromSave_; }

  // Visual-QA scenarios (query string ?qa=...): "camp", "shrine", "wolves". Returns false if unknown.
  bool applyScenario(const std::string& name);

  // Test hooks.
  Survival& survivalForTest() { return survival_; }
  Inventory& inventoryForTest() { return inventory_; }
  void placePlayerForTest(Vec3 p, float yaw) { player_.spawn(p, yaw); camera_.reset(player_); }

 private:
  void startPlaying();
  void applySave(const SaveData& d);
  void updateInteraction(const InputFrame& input);
  void updateFires(float dt);
  void updateTransition(float dt);
  bool tryBuildFire();
  void eatSomething();
  void notify(const std::string& text) { events_.push_back({GameEvent::Notify, player_.position(), 0.0f, text}); }

  GameConfig config_;
  std::unique_ptr<World> world_;
  PlayerController player_;
  ThirdPersonCamera camera_;
  CharacterAnimator animator_;
  TimeOfDay time_;
  GamePhase phase_ = GamePhase::LoadingWorld;
  double simTime_ = 0.0;
  double playSeconds_ = 0.0;

  Survival survival_;
  Inventory inventory_;
  std::vector<Pickup> pickups_;
  std::vector<Campfire> campfires_;
  Wildlife wildlife_;
  std::vector<GameEvent> events_;
  std::string prompt_;
  Vec3 restPoint_;
  bool hasRestPoint_ = false;
  int day_ = 1;
  float fireHeat_ = 0.0f;
  float drinkCooldown_ = 0.0f;
  float hintTimer_ = 0.0f;
  int hintStage_ = 0;

  Transition transition_ = Transition::None;
  float transitionTime_ = 0.0f;
  float fade_ = 0.0f;

  SaveData pendingSave_;
  bool hasPendingSave_ = false;
  bool startedFromSave_ = false;
};

}  // namespace aaa
