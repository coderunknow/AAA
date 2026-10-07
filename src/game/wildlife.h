#pragma once
// Avoidable predators: a small wolf pack. No combat — the player survives by reading the
// signs (howls, eyes in the dark), keeping a fire, or reaching the shrine.
//   Roam    → wander their territory (far away by day).
//   Stalk   → at night, circle a lone player at a distance, slowly closing in.
//   Charge  → a short committed rush; contact = one bite.
//   Retreat → after biting or being scared, break off and keep away for a while.
#include <cstdint>
#include <vector>

#include "core/math.h"
#include "game/interactables.h"

namespace aaa {

class World;

enum class WolfState : uint8_t { Roam, Stalk, Charge, Retreat };

struct Wolf {
  Vec3 pos;
  Vec3 vel;
  float yaw = 0.0f;
  WolfState state = WolfState::Roam;
  float stateTime = 0.0f;
  float stalkAngle = 0.0f;   // angle around the player while circling
  float stalkRadius = 18.0f;
  Vec2 roamTarget;
  float gaitPhase = 0.0f;
  float speed = 0.0f;        // horizontal, for animation
  float howlCooldown = 0.0f;
  uint32_t rng = 1;
};

struct WildlifeEvent {
  enum Type : uint8_t { Howl, Growl, Bite, Flee } type;
  Vec3 pos;
};

struct WildlifeContext {
  float dt = 0.0f;
  Vec3 player;
  float daylight = 1.0f;  // 0 night .. 1 day
  bool playerSafe = false;  // inside the shrine sanctuary
  const std::vector<Campfire>* fires = nullptr;
};

class Wildlife {
 public:
  void init(const World& world, uint32_t seed);
  // Returns damage dealt to the player this step (bites).
  float update(const WildlifeContext& ctx, const World& world, std::vector<WildlifeEvent>& events);
  const std::vector<Wolf>& wolves() const { return wolves_; }
  // QA: put the pack in a loose ring around `p`, already stalking.
  void gatherAround(Vec3 p, const World& world);
  // 0..1: how threatened the player should feel (drives audio tension).
  float threat() const { return threat_; }

 private:
  void setState(Wolf& w, WolfState s);
  std::vector<Wolf> wolves_;
  Vec2 den_;
  float threat_ = 0.0f;
  float packHowl_ = 30.0f;
};

}  // namespace aaa
