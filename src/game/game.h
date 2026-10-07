#pragma once
// Renderer-independent game state and simulation step.
#include <cstdint>
#include <memory>

#include "game/camera.h"
#include "game/character_animator.h"
#include "game/input.h"
#include "game/player.h"
#include "game/time_of_day.h"
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
  const CharacterAnimator& animator() const { return animator_; }
  CharacterAnimator& animator() { return animator_; }
  TimeOfDay& timeOfDay() { return time_; }
  const TimeOfDay& timeOfDay() const { return time_; }
  double simTime() const { return simTime_; }
  void setPaused(bool p);

 private:
  void startPlaying();

  GameConfig config_;
  std::unique_ptr<World> world_;
  PlayerController player_;
  ThirdPersonCamera camera_;
  CharacterAnimator animator_;
  TimeOfDay time_;
  GamePhase phase_ = GamePhase::LoadingWorld;
  double simTime_ = 0.0;
};

}  // namespace aaa
