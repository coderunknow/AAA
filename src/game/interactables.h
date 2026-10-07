#pragma once
// Things the player can gather or build: deterministic forage spots, inventory and campfires.
#include <array>
#include <cstdint>
#include <vector>

#include "core/math.h"

namespace aaa {

class World;

enum class ItemKind : uint8_t { Branch, Flint, Berries, Mushroom, Count };
constexpr int kItemKindCount = static_cast<int>(ItemKind::Count);
const char* itemName(ItemKind k, int count);

struct Inventory {
  std::array<int, kItemKindCount> count{};
  static int capacity(ItemKind k);
  int get(ItemKind k) const { return count[static_cast<int>(k)]; }
  // Adds up to capacity; returns how many were actually added.
  int add(ItemKind k, int n);
  bool take(ItemKind k, int n);
};

enum class PickupKind : uint8_t { Deadwood, FlintStone, BerryBush, Mushrooms, Count };

struct Pickup {
  uint16_t id = 0;
  PickupKind kind = PickupKind::Deadwood;
  Vec3 pos;
  float yaw = 0.0f;
  float scale = 1.0f;
  uint8_t variant = 0;
  bool available = true;
  double regrowAt = 0.0;  // play-seconds when a picked spot is available again (0 = never)
};

struct PickupYield {
  ItemKind item;
  int amount;
  double regrowSeconds;  // 0 = gone for good
};
PickupYield pickupYield(PickupKind k, uint8_t variant);

// Deterministic placement for a world seed: a guaranteed starter cache near the spawn,
// then biome-aware spots along the valley. Same seed => same ids and positions.
std::vector<Pickup> generatePickups(const World& world);

struct Campfire {
  Vec3 pos;
  float fuel = 0.0f;  // seconds of burn left
  float age = 0.0f;   // seconds since lit (for ignition VFX)
  bool burning() const { return fuel > 0.0f; }
  // 0..1 flame strength (ramps up on ignition, dies down as fuel runs out).
  float intensity() const;
  static constexpr float kStartFuel = 240.0f;
  static constexpr float kFuelPerBranch = 100.0f;
  static constexpr float kMaxFuel = 600.0f;
  static constexpr float kWarmRadius = 4.5f;
  static constexpr float kScareRadius = 16.0f;
};

// Heat felt at `p` from all burning fires (0..1).
float fireHeatAt(const std::vector<Campfire>& fires, Vec3 p);

}  // namespace aaa
