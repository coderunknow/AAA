#include "game/game.h"

#include <cmath>

#include "core/log.h"

namespace aaa {

Game::Game(const GameConfig& config) : config_(config) {
  world_ = std::make_unique<World>(config.worldSeed, config.heightfieldResolution);
}

void Game::startPlaying() {
  const WorldLayout& L = world_->layout();
  const Vec2 sp = L.spawn;
  // Face up the valley toward the shrine terrace so the landmark frames the first view.
  const Vec2 toShrine = L.shrine - sp;
  const float yaw = std::atan2(toShrine.x, toShrine.y);
  player_.spawn({sp.x, world_->groundHeight(sp.x, sp.y), sp.y}, yaw);
  world_->streamAround(player_.position(), config_.streamRadius, config_.streamReleaseRadius, 1000);
  world_->streamDetailAround(player_.position(), config_.detailRadius, config_.detailReleaseRadius, 1000);
  camera_.reset(player_);
  phase_ = GamePhase::Playing;
  AAA_LOG_INFO("world ready: spawn (%.1f, %.1f, %.1f), %d chunks streamed", player_.position().x,
               player_.position().y, player_.position().z, world_->generatedChunkCount());
}

void Game::setPaused(bool p) {
  if (phase_ == GamePhase::LoadingWorld) return;
  phase_ = p ? GamePhase::Paused : GamePhase::Playing;
}

void Game::update(float dt, const InputFrame& input, double loadBudgetMs) {
  if (phase_ == GamePhase::LoadingWorld) {
    if (world_->generateStep(loadBudgetMs)) startPlaying();
    return;
  }
  if (phase_ == GamePhase::Paused) return;

  dt = std::fmin(dt, 0.1f);  // avoid tunnelling after a hitch / tab switch
  simTime_ += dt;
  if (!time_.paused) time_.update(dt);

  // Camera-relative movement.
  const Vec2 f = camera_.groundForward(), r = camera_.groundRight();
  const Vec2 wish = f * input.move.y + r * input.move.x;
  player_.update(dt, wish, input.sprint, input.walk, input.jump, input.crouchToggle, *world_);
  camera_.update(dt, input, player_, *world_);
  animator_.update(dt, player_);
  world_->streamAround(player_.position(), config_.streamRadius, config_.streamReleaseRadius,
                       config_.maxChunksPerFrame);
  world_->streamDetailAround(player_.position(), config_.detailRadius, config_.detailReleaseRadius, 1);
}

}  // namespace aaa
