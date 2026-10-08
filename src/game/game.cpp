#include "game/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"

namespace aaa {
namespace {
constexpr float kInteractRange = 1.9f;
constexpr float kFadeSeconds = 1.6f;
constexpr int kFireBranchCost = 4;
constexpr size_t kMaxCampfires = 6;

std::string countText(int n, ItemKind k) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%d %s", n, itemName(k, n));
  return buf;
}
}  // namespace

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
  restPoint_ = world_->shrine().restPoint;
  pickups_ = generatePickups(*world_);
  wildlife_.init(*world_, config_.worldSeed);
  if (hasPendingSave_) {
    applySave(pendingSave_);
    startedFromSave_ = true;
    hasPendingSave_ = false;
  }
  world_->streamAround(player_.position(), config_.streamRadius, config_.streamReleaseRadius, 1000);
  world_->streamDetailAround(player_.position(), config_.detailRadius, config_.detailReleaseRadius, 1000);
  camera_.reset(player_);
  phase_ = GamePhase::Playing;
  AAA_LOG_INFO("world ready: spawn (%.1f, %.1f, %.1f), %d chunks streamed, %d forage spots%s", player_.position().x,
               player_.position().y, player_.position().z, world_->generatedChunkCount(),
               static_cast<int>(pickups_.size()), startedFromSave_ ? ", restored from save" : "");
}

void Game::applySave(const SaveData& d) {
  Vec3 p = d.playerPos;
  p.y = world_->groundHeight(p.x, p.z);  // never trust a saved height over the terrain
  player_.spawn(p, d.playerYaw);
  time_.setHours(d.hours);
  playSeconds_ = d.playSeconds;
  simTime_ = d.playSeconds;
  day_ = d.day;
  survival_.restore(d.vitals);
  if (survival_.collapsed()) survival_.reviveAfterCollapse();
  for (int i = 0; i < kItemKindCount; ++i) inventory_.count[i] = std::min(d.inventory[i], Inventory::capacity(ItemKind(i)));
  if (d.hasRestPoint) {
    restPoint_ = d.restPoint;
    hasRestPoint_ = true;
  }
  for (const SavedPickup& sp : d.picked)
    for (Pickup& pk : pickups_)
      if (pk.id == sp.id) {
        pk.available = false;
        pk.regrowAt = sp.regrowAt;
        break;
      }
  campfires_.clear();
  for (const SavedFire& f : d.fires) campfires_.push_back({f.pos, f.fuel, 10.0f});
}

SaveData Game::makeSave() const {
  SaveData d;
  d.worldSeed = config_.worldSeed;
  d.playerPos = player_.position();
  d.playerYaw = player_.facingYaw();
  d.hours = time_.hours();
  d.playSeconds = playSeconds_;
  d.day = day_;
  d.vitals = survival_.vitals();
  for (int i = 0; i < kItemKindCount; ++i) d.inventory[i] = inventory_.count[i];
  d.hasRestPoint = hasRestPoint_;
  d.restPoint = restPoint_;
  for (const Pickup& pk : pickups_)
    if (!pk.available && d.picked.size() < SaveData::kMaxPicked) d.picked.push_back({pk.id, pk.regrowAt});
  for (const Campfire& f : campfires_)
    if (f.burning() && d.fires.size() < SaveData::kMaxFires) d.fires.push_back({f.pos, f.fuel});
  return d;
}

void Game::setPaused(bool p) {
  if (phase_ == GamePhase::LoadingWorld) return;
  phase_ = p ? GamePhase::Paused : GamePhase::Playing;
}

void Game::restartJourney() {
  // Fresh journey in the same world (the world seed never changes): default survival,
  // the spawn point, untouched forage, no fires, the original pack, morning of day 1.
  hasPendingSave_ = false;
  startedFromSave_ = false;
  survival_ = Survival{};
  inventory_ = Inventory{};
  campfires_.clear();
  pickups_ = generatePickups(*world_);
  wildlife_.init(*world_, config_.worldSeed);
  day_ = 1;
  playSeconds_ = 0.0;
  simTime_ = 0.0;
  time_.setHours(7.4f);
  time_.paused = false;
  hasRestPoint_ = false;
  restPoint_ = world_->shrine().restPoint;
  hintTimer_ = 0.0f;
  hintStage_ = 0;
  transition_ = Transition::None;
  transitionTime_ = 0.0f;
  fade_ = 0.0f;
  prompt_.clear();
  events_.clear();
  startPlaying();
}

void Game::update(float dt, const InputFrame& input, double loadBudgetMs) {
  if (phase_ == GamePhase::LoadingWorld) {
    if (world_->generateStep(loadBudgetMs)) startPlaying();
    return;
  }
  if (phase_ == GamePhase::Paused) {
    // Keep the framing live behind the title / pause screens (no input, no simulation).
    camera_.update(dt, InputFrame{}, player_, *world_);
    animator_.update(dt, player_);
    return;
  }

  dt = std::fmin(dt, 0.1f);  // avoid tunnelling after a hitch / tab switch
  simTime_ += dt;
  playSeconds_ += dt;
  if (!time_.paused) {
    const float before = time_.hours();
    time_.update(dt);
    if (time_.hours() < before) ++day_;
  }

  const bool controllable = transition_ == Transition::None;
  InputFrame in = input;
  if (!controllable) in = InputFrame{};

  // Camera-relative movement.
  const Vec2 f = camera_.groundForward(), r = camera_.groundRight();
  const Vec2 wish = f * in.move.y + r * in.move.x;
  player_.update(dt, wish, in.sprint, in.walk, in.jump, in.crouchToggle, *world_);
  camera_.update(dt, input, player_, *world_);
  animator_.update(dt, player_);
  world_->streamAround(player_.position(), config_.streamRadius, config_.streamReleaseRadius,
                       config_.maxChunksPerFrame);
  world_->streamDetailAround(player_.position(), config_.detailRadius, config_.detailReleaseRadius, 1);

  const Vec3 pp = player_.position();
  const float landing = player_.consumeLandingSpeed();
  if (landing > 4.0f) events_.push_back({GameEvent::Land, pp, landing, {}});
  if (animator_.consumeFootstep()) {
    const float surface = player_.waterDepth() > 0.05f ? 1.0f : (world_->shrine().surfaceHeight(pp.x, pp.z) > pp.y - 0.05f ? 2.0f : 0.0f);
    events_.push_back({GameEvent::Footstep, pp, surface, {}});
  }

  updateFires(dt);
  fireHeat_ = fireHeatAt(campfires_, pp);

  // Survival.
  const ShrineLayout& sh = world_->shrine();
  const Vec2 local = sh.toLocal(pp.xz());
  const bool sheltered = std::fabs(local.x) < 4.4f && std::fabs(local.y) < 3.6f && pp.y >= sh.platformTop - 0.05f;
  SurvivalInputs si;
  si.dt = dt;
  si.airTemperature = Survival::airTemperature(time_.hours(), pp.y);
  si.fireHeat = fireHeat_;
  si.waterDepth = player_.waterDepth();
  si.sheltered = sheltered;
  si.sprinting = player_.locomotion() == Locomotion::Sprint;
  si.landingSpeed = landing;
  if (transition_ == Transition::None) survival_.update(si);

  // Wildlife.
  std::vector<WildlifeEvent> wev;
  WildlifeContext wc;
  wc.dt = dt;
  wc.player = pp;
  wc.daylight = time_.daylight();
  wc.playerSafe = sh.inSanctuary(pp.xz()) || transition_ != Transition::None;
  wc.fires = &campfires_;
  const float bite = wildlife_.update(wc, *world_, wev);
  if (bite > 0.0f && transition_ == Transition::None) survival_.damage(bite, DamageCause::Predator);
  for (const WildlifeEvent& e : wev) {
    const GameEvent::Type t = e.type == WildlifeEvent::Howl    ? GameEvent::Howl
                              : e.type == WildlifeEvent::Growl ? GameEvent::Growl
                              : e.type == WildlifeEvent::Bite  ? GameEvent::Bite
                                                               : GameEvent::WolfFlee;
    events_.push_back({t, e.pos, 0.0f, {}});
  }

  // Regrowth of berry bushes / mushrooms.
  for (Pickup& pk : pickups_)
    if (!pk.available && pk.regrowAt > 0.0 && playSeconds_ >= pk.regrowAt) pk.available = true;

  if (survival_.collapsed() && transition_ == Transition::None) {
    transition_ = Transition::CollapseOut;
    transitionTime_ = 0.0f;
    events_.push_back({GameEvent::Collapse, pp, 0.0f, damageCauseText(survival_.lastDamage())});
  }

  drinkCooldown_ = std::fmax(0.0f, drinkCooldown_ - dt);
  if (controllable) updateInteraction(input);
  else prompt_.clear();
  updateTransition(dt);

  // Gentle, diegetic-first onboarding: a few one-time hints, only when relevant.
  hintTimer_ += dt;
  if (!startedFromSave_) {
    if (hintStage_ == 0 && hintTimer_ > 4.0f) {
      notify("The valley is cold at night. Gather dry branches and flint before dusk.");
      ++hintStage_;
    } else if (hintStage_ == 1 && inventory_.get(ItemKind::Branch) >= kFireBranchCost && inventory_.get(ItemKind::Flint) >= 1) {
      notify("You could build a fire now  [F].");
      ++hintStage_;
    } else if (hintStage_ == 2 && time_.daylight() < 0.35f) {
      notify("Night is falling. Stay by a fire, or seek the old shrine up the trail.");
      ++hintStage_;
    }
  }
  if (survival_.vitals().hydration < 20.0f && hintTimer_ > 45.0f) {
    notify("Your throat is dry. The stream runs through the valley floor.");
    hintTimer_ = 0.0f;
  }
}

void Game::updateFires(float dt) {
  for (Campfire& c : campfires_) {
    if (!c.burning()) continue;
    c.age += dt;
    c.fuel = std::fmax(0.0f, c.fuel - dt);
    if (!c.burning()) events_.push_back({GameEvent::FireOut, c.pos, 0.0f, {}});
  }
}

void Game::updateInteraction(const InputFrame& input) {
  prompt_.clear();
  // Build / eat work regardless of what is in front of the player.
  if (input.buildFire) tryBuildFire();
  if (input.eat) eatSomething();
  const Vec3 pp = player_.position();
  const Vec2 facing{std::sin(player_.facingYaw()), std::cos(player_.facingYaw())};

  // 1) Nearest available forage spot in front of the player.
  Pickup* best = nullptr;
  float bestScore = 1e9f;
  for (Pickup& pk : pickups_) {
    if (!pk.available) continue;
    const Vec2 d = pk.pos.xz() - pp.xz();
    const float dist = length(d);
    if (dist > kInteractRange || std::fabs(pk.pos.y - pp.y) > 1.5f) continue;
    const float facingDot = dist > 1e-3f ? dot(d * (1.0f / dist), facing) : 1.0f;
    if (facingDot < -0.3f) continue;
    const float score = dist - facingDot * 0.6f;
    if (score < bestScore) { bestScore = score; best = &pk; }
  }
  if (best) {
    const PickupYield y = pickupYield(best->kind, best->variant);
    const char* verb = best->kind == PickupKind::BerryBush ? "Pick berries" :
                       best->kind == PickupKind::Mushrooms ? "Gather mushrooms" :
                       best->kind == PickupKind::FlintStone ? "Take flint" : "Gather dry branches";
    prompt_ = std::string("E  ") + verb;
    if (input.interact) {
      const int added = inventory_.add(y.item, y.amount);
      if (added == 0) {
        notify("You can't carry any more " + std::string(itemName(y.item, 2)) + ".");
      } else {
        best->available = false;
        best->regrowAt = y.regrowSeconds > 0.0 ? playSeconds_ + y.regrowSeconds : 0.0;
        events_.push_back({GameEvent::Pickup, best->pos, static_cast<float>(y.item), "+" + countText(added, y.item)});
      }
    }
    return;
  }

  // 2) A burning campfire within reach: feed it.
  for (Campfire& c : campfires_) {
    if (length(c.pos.xz() - pp.xz()) > 2.0f) continue;
    if (c.burning()) {
      if (inventory_.get(ItemKind::Branch) > 0) {
        prompt_ = "E  Add a branch to the fire";
        if (input.interact && c.fuel < Campfire::kMaxFuel - 10.0f) {
          inventory_.take(ItemKind::Branch, 1);
          c.fuel = std::fmin(Campfire::kMaxFuel, c.fuel + Campfire::kFuelPerBranch);
          events_.push_back({GameEvent::FireFed, c.pos, 0.0f, {}});
        }
      }
    } else if (inventory_.get(ItemKind::Branch) >= 2) {
      prompt_ = "E  Rekindle the fire (2 branches)";
      if (input.interact) {
        inventory_.take(ItemKind::Branch, 2);
        c.fuel = Campfire::kStartFuel * 0.6f;
        c.age = 0.0f;
        events_.push_back({GameEvent::FireLit, c.pos, 0.0f, "The embers catch again."});
      }
    }
    if (!prompt_.empty()) return;
  }

  // 3) The shrine altar: rest (sets the wake point, passes time, saves).
  const ShrineLayout& sh = world_->shrine();
  if (length(sh.altar.xz() - pp.xz()) < 2.6f && pp.y > sh.platformTop - 0.3f) {
    prompt_ = "E  Rest at the shrine";
    if (input.interact) {
      transition_ = Transition::RestOut;
      transitionTime_ = 0.0f;
      restPoint_ = sh.restPoint;
      hasRestPoint_ = true;
    }
    return;
  }

  // 4) Water ahead or underfoot: drink.
  const Vec3 ahead = pp + Vec3{facing.x, 0.0f, facing.y} * 1.1f;
  if (world_->waterDepth(ahead.x, ahead.z) > 0.08f || player_.waterDepth() > 0.08f) {
    prompt_ = "E  Drink";
    if (input.interact && drinkCooldown_ <= 0.0f) {
      survival_.drink(22.0f);
      drinkCooldown_ = 0.8f;
      events_.push_back({GameEvent::Drink, ahead, 0.0f, {}});
    }
  }

  if (prompt_.empty() && inventory_.get(ItemKind::Branch) >= kFireBranchCost && inventory_.get(ItemKind::Flint) >= 1 &&
      time_.daylight() < 0.5f && fireHeat_ < 0.05f)
    prompt_ = "F  Build a campfire";
}

bool Game::tryBuildFire() {
  if (inventory_.get(ItemKind::Branch) < kFireBranchCost || inventory_.get(ItemKind::Flint) < 1) {
    notify("A fire needs 4 dry branches and a flint.");
    return false;
  }
  const Vec3 pp = player_.position();
  const Vec2 facing{std::sin(player_.facingYaw()), std::cos(player_.facingYaw())};
  const Vec2 spot = pp.xz() + facing * 1.3f;
  if (world_->waterDepth(spot.x, spot.y) > 0.0f || player_.waterDepth() > 0.05f) {
    notify("The ground here is too wet for a fire.");
    return false;
  }
  if (world_->slope(spot.x, spot.y) > 0.3f) {
    notify("Too steep. Find flatter ground.");
    return false;
  }
  for (const Campfire& c : campfires_)
    if (c.burning() && length(c.pos.xz() - spot) < 3.0f) {
      notify("There is already a fire here.");
      return false;
    }
  bool blocked = false;
  world_->forEachColliderNear(spot, 0.6f, [&](const CircleCollider&) { blocked = true; });
  if (blocked) {
    notify("No room for a fire here.");
    return false;
  }
  inventory_.take(ItemKind::Branch, kFireBranchCost);
  inventory_.take(ItemKind::Flint, 1);
  if (campfires_.size() >= kMaxCampfires) {
    // Replace the oldest dead fire, else the oldest fire.
    auto it = std::find_if(campfires_.begin(), campfires_.end(), [](const Campfire& c) { return !c.burning(); });
    campfires_.erase(it != campfires_.end() ? it : campfires_.begin());
  }
  const Vec3 pos{spot.x, world_->groundHeight(spot.x, spot.y), spot.y};
  campfires_.push_back({pos, Campfire::kStartFuel, 0.0f});
  events_.push_back({GameEvent::FireLit, pos, 0.0f, "Sparks catch in the tinder. The fire takes."});
  events_.push_back({GameEvent::RequestSave, pos, 0.0f, {}});
  return true;
}

void Game::eatSomething() {
  if (inventory_.take(ItemKind::Berries, 1)) {
    survival_.eat(11.0f, 4.0f);
    events_.push_back({GameEvent::Eat, player_.position(), 0.0f, "You eat a handful of berries."});
  } else if (inventory_.take(ItemKind::Mushroom, 1)) {
    survival_.eat(16.0f, 1.0f);
    events_.push_back({GameEvent::Eat, player_.position(), 0.0f, "You eat a mushroom. Earthy, filling."});
  } else {
    notify("You have nothing to eat.");
  }
}

void Game::updateTransition(float dt) {
  if (transition_ == Transition::None) {
    fade_ = std::fmax(0.0f, fade_ - dt / kFadeSeconds);
    return;
  }
  transitionTime_ += dt;
  const bool out = transition_ == Transition::RestOut || transition_ == Transition::CollapseOut;
  const float dur = transition_ == Transition::CollapseOut ? kFadeSeconds * 1.8f : kFadeSeconds;
  if (out) fade_ = std::fmin(1.0f, transitionTime_ / dur);
  if (out && transitionTime_ >= dur + 0.6f) {
    if (transition_ == Transition::RestOut) {
      // Rest until morning if it's night, otherwise for two hours.
      const float h = time_.hours();
      const float target = (h > 18.0f || h < 6.0f) ? 7.0f : std::fmod(h + 2.0f, 24.0f);
      if (target < h) ++day_;
      time_.setHours(target);
      Vitals v = survival_.vitals();
      v.warmth = 100.0f;
      v.wetness = 0.0f;
      v.health = std::fmin(100.0f, v.health + 30.0f);
      v.satiety = std::fmax(0.0f, v.satiety - 12.0f);
      v.hydration = std::fmax(0.0f, v.hydration - 15.0f);
      survival_.restore(v);
      events_.push_back({GameEvent::Rest, player_.position(), 0.0f, "You rest beneath the old roof. The shrine will remember you."});
      events_.push_back({GameEvent::RequestSave, player_.position(), 0.0f, {}});
      transition_ = Transition::RestIn;
    } else {
      // Wake at the last rest point (the shrine by default), hurt but alive; some carried food is lost.
      const float h = time_.hours();
      const float target = std::fmod(h + 4.0f, 24.0f);
      if (target < h) ++day_;
      time_.setHours(target);
      player_.spawn({restPoint_.x, world_->groundHeight(restPoint_.x, restPoint_.z), restPoint_.z},
                    world_->shrine().yaw + kPi);
      camera_.reset(player_);
      survival_.reviveAfterCollapse();
      inventory_.count[static_cast<int>(ItemKind::Berries)] /= 2;
      inventory_.count[static_cast<int>(ItemKind::Mushroom)] /= 2;
      events_.push_back({GameEvent::Wake, player_.position(), 0.0f, "You wake at the shrine, aching and half-frozen, but alive."});
      events_.push_back({GameEvent::RequestSave, player_.position(), 0.0f, {}});
      transition_ = Transition::CollapseIn;
    }
    transitionTime_ = 0.0f;
    return;
  }
  if (!out) {
    fade_ = std::fmax(0.0f, 1.0f - transitionTime_ / (kFadeSeconds * 1.5f));
    if (fade_ <= 0.0f) transition_ = Transition::None;
  }
}

bool Game::applyScenario(const std::string& name) {
  const Vec3 pp = player_.position();
  const Vec2 facing{std::sin(player_.facingYaw()), std::cos(player_.facingYaw())};
  if (name == "camp") {
    const Vec2 spot = pp.xz() + facing * 2.6f + Vec2{facing.y, -facing.x} * 0.6f;
    campfires_.push_back({{spot.x, world_->groundHeight(spot.x, spot.y), spot.y}, Campfire::kMaxFuel, 10.0f});
    inventory_.add(ItemKind::Branch, 5);
    inventory_.add(ItemKind::Berries, 2);
    return true;
  }
  if (name == "shrine") {
    const ShrineLayout& sh = world_->shrine();
    const Vec3 p = sh.toWorld({1.5f, 0.0f, 19.0f});
    player_.spawn({p.x, world_->groundHeight(p.x, p.z), p.z}, sh.yaw + kPi);
    camera_.reset(player_);
    return true;
  }
  if (name == "wolves") {
    wildlife_.gatherAround(pp + Vec3{facing.x, 0.0f, facing.y} * 6.0f, *world_);
    return true;
  }
  return false;
}

}  // namespace aaa
