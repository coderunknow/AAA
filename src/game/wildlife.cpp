#include "game/wildlife.h"

#include <algorithm>
#include <cmath>

#include "core/rng.h"
#include "world/world.h"

namespace aaa {
namespace {
float rand01(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return hashToFloat(s);
}

bool scaredByFire(const std::vector<Campfire>* fires, Vec3 p, float extra) {
  if (!fires) return false;
  for (const Campfire& f : *fires)
    if (f.burning() && length(p.xz() - f.pos.xz()) < Campfire::kScareRadius + extra) return true;
  return false;
}
}  // namespace

void Wildlife::init(const World& world, uint32_t seed) {
  const WorldLayout& L = world.layout();
  // The den sits in the karst field farthest from the spawn: wolves arrive from the wilds.
  den_ = L.valley.pointAt(0.85f);
  float best = -1.0f;
  for (const Clearing& k : L.karstFields) {
    const float d = length(k.center - L.spawn);
    if (d > best) { best = d; den_ = k.center; }
  }
  wolves_.clear();
  for (int i = 0; i < 3; ++i) {
    Wolf w;
    w.rng = hashCombine(seed, 0x701f + i) | 1u;
    const float a = rand01(w.rng) * kTwoPi;
    w.pos = {den_.x + std::sin(a) * 12.0f, 0.0f, den_.y + std::cos(a) * 12.0f};
    w.pos.y = world.groundHeight(w.pos.x, w.pos.z);
    w.roamTarget = w.pos.xz();
    w.stalkAngle = a;
    w.howlCooldown = 20.0f + 30.0f * rand01(w.rng);
    wolves_.push_back(w);
  }
}

void Wildlife::gatherAround(Vec3 p, const World& world) {
  for (size_t i = 0; i < wolves_.size(); ++i) {
    Wolf& w = wolves_[i];
    const float a = 0.5f + static_cast<float>(i) * 0.55f;
    const float r = 9.0f + 2.5f * static_cast<float>(i);
    w.pos = {p.x + std::sin(a) * r, 0.0f, p.z + std::cos(a) * r};
    w.pos.y = world.groundHeight(w.pos.x, w.pos.z);
    w.yaw = std::atan2(p.x - w.pos.x, p.z - w.pos.z);
    w.stalkAngle = std::atan2(w.pos.x - p.x, w.pos.z - p.z);
    w.stalkRadius = r;
    setState(w, WolfState::Stalk);
  }
}

void Wildlife::setState(Wolf& w, WolfState s) {
  w.state = s;
  w.stateTime = 0.0f;
}

float Wildlife::update(const WildlifeContext& ctx, const World& world, std::vector<WildlifeEvent>& events) {
  const float dt = ctx.dt;
  const float night = 1.0f - smoothstep(0.15f, 0.45f, ctx.daylight);
  float damage = 0.0f;
  float threat = 0.0f;
  const float half = world.layout().worldSize * 0.5f - 10.0f;

  // Occasional pack howl at night from wherever the pack is (audible warning).
  packHowl_ -= dt;
  if (night > 0.5f && packHowl_ <= 0.0f && !wolves_.empty()) {
    events.push_back({WildlifeEvent::Howl, wolves_[0].pos});
    packHowl_ = 50.0f + 40.0f * rand01(wolves_[0].rng);
  }

  for (Wolf& w : wolves_) {
    w.stateTime += dt;
    const Vec2 toPlayer = ctx.player.xz() - w.pos.xz();
    const float distPlayer = length(toPlayer);
    const bool fireNearWolf = scaredByFire(ctx.fires, w.pos, 0.0f);
    const bool playerByFire = scaredByFire(ctx.fires, ctx.player, -6.0f);
    const bool playerProtected = ctx.playerSafe || playerByFire;
    Vec2 desired{0.0f, 0.0f};
    float maxSpeed = 2.0f;

    switch (w.state) {
      case WolfState::Roam: {
        // Hunt grounds shift toward the player at night, stay near the den by day.
        const Vec2 centre =
            night > 0.5f ? Vec2{lerp(den_.x, ctx.player.x, 0.6f), lerp(den_.y, ctx.player.z, 0.6f)} : den_;
        if (length(w.roamTarget - w.pos.xz()) < 3.0f || w.stateTime > 25.0f) {
          const float a = rand01(w.rng) * kTwoPi, r = 10.0f + 50.0f * rand01(w.rng);
          w.roamTarget = centre + Vec2{std::sin(a) * r, std::cos(a) * r};
          w.stateTime = 0.0f;
        }
        desired = w.roamTarget - w.pos.xz();
        maxSpeed = 1.6f;
        if (night > 0.6f && distPlayer < 55.0f && !playerProtected) {
          setState(w, WolfState::Stalk);
          w.stalkAngle = std::atan2(-toPlayer.x, -toPlayer.y);
          w.stalkRadius = 18.0f + 6.0f * rand01(w.rng);
          events.push_back({WildlifeEvent::Growl, w.pos});
        }
        break;
      }
      case WolfState::Stalk: {
        // Circle the player, tightening the ring over time.
        w.stalkAngle += dt * (0.18f + 0.05f * static_cast<float>(&w - wolves_.data()));
        w.stalkRadius = std::fmax(9.0f, w.stalkRadius - dt * 0.35f);
        const Vec2 ring = ctx.player.xz() + Vec2{std::sin(w.stalkAngle), std::cos(w.stalkAngle)} * w.stalkRadius;
        desired = ring - w.pos.xz();
        maxSpeed = 3.2f;
        threat = std::fmax(threat, 1.0f - smoothstep(8.0f, 40.0f, distPlayer));
        if (playerProtected || fireNearWolf || night < 0.3f) {
          setState(w, WolfState::Retreat);
          events.push_back({WildlifeEvent::Flee, w.pos});
        } else if (w.stateTime > 22.0f + 10.0f * rand01(w.rng) && distPlayer < 16.0f) {
          setState(w, WolfState::Charge);
          events.push_back({WildlifeEvent::Growl, w.pos});
        } else if (distPlayer > 80.0f) {
          setState(w, WolfState::Roam);
        }
        break;
      }
      case WolfState::Charge: {
        desired = toPlayer;
        maxSpeed = 8.5f;
        threat = 1.0f;
        if (distPlayer < 1.1f) {
          damage += 18.0f;
          events.push_back({WildlifeEvent::Bite, w.pos});
          setState(w, WolfState::Retreat);
        } else if (playerProtected || fireNearWolf || w.stateTime > 4.0f) {
          setState(w, WolfState::Retreat);
          events.push_back({WildlifeEvent::Flee, w.pos});
        }
        break;
      }
      case WolfState::Retreat: {
        Vec2 away = distPlayer > 1e-3f ? toPlayer * (-1.0f / distPlayer) : Vec2{1, 0};
        desired = away * 10.0f;
        maxSpeed = w.stateTime < 3.0f ? 7.0f : 3.0f;
        if (w.stateTime > 30.0f && distPlayer > 45.0f) setState(w, WolfState::Roam);
        break;
      }
    }

    // Steering: accelerate toward the desired velocity, slowed by steep ground.
    const float dl = length(desired);
    Vec2 targetVel = dl > 1e-3f ? desired * (maxSpeed / dl) : Vec2{0, 0};
    if (w.state == WolfState::Roam && dl < 2.0f) targetVel = targetVel * (dl / 2.0f);
    const float k = dampFactor(0.25f, dt);
    w.vel.x = lerp(w.vel.x, targetVel.x, k);
    w.vel.z = lerp(w.vel.z, targetVel.y, k);
    Vec3 next = w.pos + Vec3{w.vel.x, 0.0f, w.vel.z} * dt;
    next.x = clampf(next.x, -half, half);
    next.z = clampf(next.z, -half, half);
    // Avoid wading into deep water and climbing cliffs.
    if (world.waterDepth(next.x, next.z) > 0.6f || world.slope(next.x, next.z) > 0.55f) {
      w.vel = w.vel * -0.3f;
      next = w.pos;
      w.roamTarget = w.pos.xz() + Vec2{-w.vel.z, w.vel.x} * 8.0f;
    }
    // Keep out of the sanctuary entirely.
    const ShrineLayout& sh = world.shrine();
    const Vec2 fromShrine = next.xz() - sh.center;
    const float ds = length(fromShrine);
    if (ds < 16.0f && ds > 1e-3f) {
      const Vec2 pushed = sh.center + fromShrine * (16.0f / ds);
      next.x = pushed.x;
      next.z = pushed.y;
    }
    next.y = world.groundHeight(next.x, next.z);
    w.pos = next;
    w.speed = length(Vec2{w.vel.x, w.vel.z});
    if (w.speed > 0.15f) {
      const float targetYaw = std::atan2(w.vel.x, w.vel.z);
      w.yaw = wrapAngle(w.yaw + wrapAngle(targetYaw - w.yaw) * dampFactor(0.12f, dt));
    }
    w.gaitPhase = std::fmod(w.gaitPhase + dt * (0.6f + w.speed * 0.95f), 1.0f);
  }
  threat_ = lerp(threat_, threat, dampFactor(0.8f, dt));
  return damage;
}

}  // namespace aaa
