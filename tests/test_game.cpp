#include "game/game.h"
#include "test.h"

using namespace aaa;

namespace {
Game& sharedGame() {
  static Game* g = [] {
    GameConfig cfg;
    cfg.heightfieldResolution = 513;
    auto* game = new Game(cfg);
    InputFrame none;
    for (int i = 0; i < 10000 && game->phase() == GamePhase::LoadingWorld; ++i) game->update(1.0f / 60.0f, none, 50.0);
    return game;
  }();
  return *g;
}
}  // namespace

TEST_CASE("game: loads the world and spawns the player on the ground") {
  Game& g = sharedGame();
  CHECK(g.phase() == GamePhase::Playing);
  const Vec3 p = g.player().position();
  CHECK_NEAR(p.y, g.world().groundHeight(p.x, p.z), 0.05);
  CHECK(g.world().generatedChunkCount() > 0);
}

TEST_CASE("game: forward input moves the player in the camera direction") {
  Game& g = sharedGame();
  const Vec3 start = g.player().position();
  InputFrame in;
  in.move = {0.0f, 1.0f};
  for (int i = 0; i < 120; ++i) g.update(1.0f / 60.0f, in, 0.0);
  const Vec3 end = g.player().position();
  const Vec2 moved = Vec2{end.x - start.x, end.z - start.z};
  CHECK(length(moved) > 2.0f);  // ~2 s of jogging, minus acceleration and obstacles
  const Vec2 fwd = g.camera().groundForward();
  CHECK(dot(moved * (1.0f / length(moved)), fwd) > 0.5f);
  CHECK_NEAR(end.y, g.world().groundHeight(end.x, end.z), 0.5);
}

TEST_CASE("game: sprint drains stamina, rest restores it") {
  Game& g = sharedGame();
  InputFrame in;
  in.move = {0.0f, 1.0f};
  in.sprint = true;
  const float before = g.player().stamina();
  for (int i = 0; i < 120; ++i) g.update(1.0f / 60.0f, in, 0.0);
  const float after = g.player().stamina();
  CHECK(after < before - 5.0f);
  InputFrame idle;
  for (int i = 0; i < 300; ++i) g.update(1.0f / 60.0f, idle, 0.0);
  CHECK(g.player().stamina() > after + 5.0f);
}

TEST_CASE("game: jump leaves the ground and lands again") {
  Game& g = sharedGame();
  InputFrame idle;
  for (int i = 0; i < 30; ++i) g.update(1.0f / 60.0f, idle, 0.0);
  InputFrame jump;
  jump.jump = true;
  g.update(1.0f / 60.0f, jump, 0.0);
  bool wasAirborne = false;
  for (int i = 0; i < 90; ++i) {
    g.update(1.0f / 60.0f, idle, 0.0);
    wasAirborne |= !g.player().grounded();
  }
  CHECK(wasAirborne);
  CHECK(g.player().grounded());
}

TEST_CASE("camera: never below the terrain and keeps the player in front") {
  Game& g = sharedGame();
  InputFrame in;
  for (int i = 0; i < 240; ++i) {
    in.lookDelta = {7.0f, (i % 60 < 30) ? 4.0f : -4.0f};
    g.update(1.0f / 60.0f, in, 0.0);
    const CameraView& v = g.camera().view();
    CHECK(v.eye.y >= g.world().groundHeight(v.eye.x, v.eye.z) + 0.2f);
    const Vec3 toPlayer = g.player().position() + Vec3{0, 1.2f, 0} - v.eye;
    const Vec3 fwd = normalize(v.target - v.eye);
    CHECK(dot(normalize(toPlayer), fwd) > 0.5f);
  }
}

TEST_CASE("animator: produces finite poses and footsteps while moving") {
  Game& g = sharedGame();
  InputFrame in;
  in.move = {0.3f, 1.0f};
  int steps = 0;
  for (int i = 0; i < 180; ++i) {
    g.update(1.0f / 60.0f, in, 0.0);
    // Footfalls are surfaced as game events (consumed from the animator by Game::update).
    for (const GameEvent& e : g.events()) steps += e.type == GameEvent::Footstep ? 1 : 0;
    g.events().clear();
  }
  CHECK(steps >= 3);
  for (const PartPose& p : g.animator().parts()) {
    const Vec3 pos = p.transform.position();
    CHECK(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z));
    CHECK(length(pos - g.player().position()) < 3.0f);
  }
}

TEST_CASE("time of day: sun above horizon at noon, below at midnight") {
  TimeOfDay t;
  t.setHours(12.0f);
  CHECK(t.sunDirection().y > 0.6f);
  t.setHours(0.0f);
  CHECK(t.sunDirection().y < 0.0f);
  CHECK(t.daylight() == 0.0f);
}
