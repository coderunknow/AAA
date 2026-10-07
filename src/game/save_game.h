#pragma once
// Versioned save data. Pure (de)serialisation + migration + validation; the platform's
// KeyValueStorage decides where the string lives (localStorage on the web).
#include <cstdint>
#include <string>

#include "core/math.h"

namespace aaa {

struct SaveData {
  static constexpr int kCurrentVersion = 2;
  uint32_t worldSeed = 0;
  Vec3 playerPos;
  float playerYaw = 0.0f;
  float hours = 8.0f;
  double playSeconds = 0.0;
  int quality = 2;            // QualityPreset
  float mouseSensitivity = 1.0f;
  bool invertY = false;
};

enum class SaveLoadResult { Ok, Migrated, Empty, Corrupt, UnsupportedVersion, WrongWorld, Invalid };
const char* saveLoadResultName(SaveLoadResult r);

std::string serializeSave(const SaveData& d);
// Parses, migrates older versions and validates. `out` is only written on Ok / Migrated.
SaveLoadResult deserializeSave(const std::string& text, uint32_t expectedSeed, SaveData& out);

}  // namespace aaa
