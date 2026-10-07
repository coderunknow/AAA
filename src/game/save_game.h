#pragma once
// Versioned save data. Pure (de)serialisation + migration + validation; the platform's
// KeyValueStorage decides where the string lives (localStorage on the web).
#include <cstdint>
#include <string>
#include <vector>

#include "game/survival.h"

#include "core/math.h"

namespace aaa {

struct SavedPickup {
  uint16_t id = 0;
  double regrowAt = 0.0;  // 0 = never regrows
};
struct SavedFire {
  Vec3 pos;
  float fuel = 0.0f;
};

struct SaveData {
  static constexpr int kCurrentVersion = 3;
  static constexpr size_t kMaxPicked = 2048;
  static constexpr size_t kMaxFires = 8;
  uint32_t worldSeed = 0;
  Vec3 playerPos;
  float playerYaw = 0.0f;
  float hours = 8.0f;
  double playSeconds = 0.0;
  int quality = 2;            // QualityPreset
  float mouseSensitivity = 1.0f;
  bool invertY = false;
  // v3: survival state.
  int day = 1;
  Vitals vitals;
  int inventory[4] = {0, 0, 0, 0};  // ItemKind order: branch, flint, berries, mushroom
  bool hasRestPoint = false;
  Vec3 restPoint;
  std::vector<SavedPickup> picked;
  std::vector<SavedFire> fires;
};

enum class SaveLoadResult { Ok, Migrated, Empty, Corrupt, UnsupportedVersion, WrongWorld, Invalid };
const char* saveLoadResultName(SaveLoadResult r);

std::string serializeSave(const SaveData& d);
// Parses, migrates older versions and validates. `out` is only written on Ok / Migrated.
SaveLoadResult deserializeSave(const std::string& text, uint32_t expectedSeed, SaveData& out);

}  // namespace aaa
