#include "game/interactables.h"

#include <algorithm>
#include <cmath>

#include "core/rng.h"
#include "world/world.h"

namespace aaa {

const char* itemName(ItemKind k, int count) {
  const bool one = count == 1;
  switch (k) {
    case ItemKind::Branch: return one ? "dry branch" : "dry branches";
    case ItemKind::Flint: return one ? "flint" : "flints";
    case ItemKind::Berries: return one ? "handful of berries" : "handfuls of berries";
    case ItemKind::Mushroom: return one ? "mushroom" : "mushrooms";
    case ItemKind::Count: break;
  }
  return "?";
}

int Inventory::capacity(ItemKind k) {
  switch (k) {
    case ItemKind::Branch: return 16;
    case ItemKind::Flint: return 4;
    case ItemKind::Berries: return 8;
    case ItemKind::Mushroom: return 6;
    case ItemKind::Count: break;
  }
  return 0;
}

int Inventory::add(ItemKind k, int n) {
  int& c = count[static_cast<int>(k)];
  const int added = std::max(0, std::min(n, capacity(k) - c));
  c += added;
  return added;
}

bool Inventory::take(ItemKind k, int n) {
  int& c = count[static_cast<int>(k)];
  if (c < n) return false;
  c -= n;
  return true;
}

PickupYield pickupYield(PickupKind k, uint8_t variant) {
  switch (k) {
    case PickupKind::Deadwood: return {ItemKind::Branch, 2 + variant % 2, 0.0};
    case PickupKind::FlintStone: return {ItemKind::Flint, 1, 0.0};
    case PickupKind::BerryBush: return {ItemKind::Berries, 2, 600.0};  // bushes fruit again
    case PickupKind::Mushrooms: return {ItemKind::Mushroom, 1 + variant % 2, 900.0};
    case PickupKind::Count: break;
  }
  return {ItemKind::Branch, 0, 0.0};
}

float Campfire::intensity() const {
  if (fuel <= 0.0f) return 0.0f;
  return smoothstep(0.0f, 2.5f, age) * (0.35f + 0.65f * smoothstep(0.0f, 60.0f, fuel));
}

float fireHeatAt(const std::vector<Campfire>& fires, Vec3 p) {
  float heat = 0.0f;
  for (const Campfire& f : fires) {
    if (!f.burning()) continue;
    const float d = length(p - f.pos);
    heat = std::fmax(heat, f.intensity() * (1.0f - smoothstep(1.0f, Campfire::kWarmRadius, d)));
  }
  return heat;
}

namespace {
struct Rule {
  PickupKind kind;
  int count;
};

bool validSpot(const World& w, PickupKind kind, float x, float z) {
  const float half = w.layout().worldSize * 0.5f - 24.0f;
  if (std::fabs(x) > half || std::fabs(z) > half) return false;
  if (w.slope(x, z) > (kind == PickupKind::FlintStone ? 0.35f : 0.22f)) return false;
  const float stream = w.fields().stream(x, z);
  if (stream < 5.5f) return false;          // keep out of the water and its banks
  if (w.fields().trail(x, z) < 1.2f) return false;
  if (w.shrine().inSanctuary({x, z})) return false;
  // NOTE: deliberately no streamed-collider test here — placement must depend only on the seed
  // (save files reference pickups by id), never on which chunks happen to be loaded.
  switch (kind) {
    case PickupKind::FlintStone: return stream < 30.0f || w.slope(x, z) > 0.12f;  // banks and scree
    case PickupKind::Mushrooms: return w.fields().valley(x, z) < 120.0f;         // damp valley floor
    default: return true;
  }
}
}  // namespace

std::vector<Pickup> generatePickups(const World& world) {
  std::vector<Pickup> out;
  const WorldLayout& L = world.layout();
  Rng rng(hashCombine(L.seed, 0x0f0a6e5u));
  uint16_t nextId = 1;
  auto place = [&](PickupKind kind, float x, float z) {
    Pickup p;
    p.id = nextId++;
    p.kind = kind;
    p.pos = {x, world.groundHeight(x, z), z};
    p.yaw = rng.range(0.0f, kTwoPi);
    p.scale = rng.range(0.85f, 1.2f);
    p.variant = static_cast<uint8_t>(rng.rangeInt(0, 3));
    out.push_back(p);
  };

  // Starter cache: enough for one fire and a snack within sight of the spawn clearing,
  // so the first minutes teach the loop without a tutorial.
  const Rule starter[] = {{PickupKind::Deadwood, 3}, {PickupKind::FlintStone, 1}, {PickupKind::BerryBush, 2},
                          {PickupKind::Mushrooms, 1}};
  for (const Rule& r : starter) {
    for (int i = 0, placed = 0; placed < r.count && i < 400; ++i) {
      const float a = rng.range(0.0f, kTwoPi), d = rng.range(6.0f, 26.0f);
      const float x = L.spawn.x + std::sin(a) * d, z = L.spawn.y + std::cos(a) * d;
      if (!validSpot(world, r.kind, x, z)) continue;
      place(r.kind, x, z);
      ++placed;
    }
  }

  // The wider valley: denser near the trail corridor, thinning out into the wilds.
  const Rule rules[] = {{PickupKind::Deadwood, 110}, {PickupKind::FlintStone, 34}, {PickupKind::BerryBush, 60},
                        {PickupKind::Mushrooms, 50}};
  for (const Rule& r : rules) {
    for (int i = 0, placed = 0; placed < r.count && i < r.count * 60; ++i) {
      // Sample along the valley with a lateral offset (most of the playable space).
      const float t = rng.nextFloat();
      const Vec2 c = L.valley.pointAt(t);
      const float lateral = (rng.nextFloat() * 2.0f - 1.0f);
      const float spread = 25.0f + 140.0f * lateral * lateral;
      const float a = rng.range(0.0f, kTwoPi);
      const float x = c.x + std::sin(a) * spread, z = c.y + std::cos(a) * spread;
      if (!validSpot(world, r.kind, x, z)) continue;
      bool crowded = false;
      for (const Pickup& o : out)
        if (std::fabs(o.pos.x - x) < 3.0f && std::fabs(o.pos.z - z) < 3.0f) { crowded = true; break; }
      if (crowded) continue;
      place(r.kind, x, z);
      ++placed;
    }
  }
  return out;
}

}  // namespace aaa
