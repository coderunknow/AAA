#include "game/save_game.h"
#include "test.h"

using namespace aaa;

namespace {
SaveData sample() {
  SaveData d;
  d.worldSeed = 1234;
  d.playerPos = {12.5f, 40.25f, -88.0f};
  d.playerYaw = 1.25f;
  d.hours = 17.5f;
  d.playSeconds = 321.5;
  d.quality = 1;
  d.mouseSensitivity = 1.4f;
  d.invertY = true;
  return d;
}
}  // namespace

TEST_CASE("save: round trip preserves every field") {
  const SaveData a = sample();
  SaveData b;
  CHECK(deserializeSave(serializeSave(a), 1234, b) == SaveLoadResult::Ok);
  CHECK_NEAR(b.playerPos.x, a.playerPos.x, 1e-5);
  CHECK_NEAR(b.playerPos.y, a.playerPos.y, 1e-5);
  CHECK_NEAR(b.playerPos.z, a.playerPos.z, 1e-5);
  CHECK_NEAR(b.playerYaw, a.playerYaw, 1e-6);
  CHECK_NEAR(b.hours, a.hours, 1e-6);
  CHECK_NEAR(b.playSeconds, a.playSeconds, 1e-6);
  CHECK(b.quality == 1);
  CHECK_NEAR(b.mouseSensitivity, 1.4, 1e-6);
  CHECK(b.invertY);
}

TEST_CASE("save: v1 saves migrate to the current schema") {
  const std::string v1 = R"({"version":1,"seed":1234,"px":3,"py":20,"pz":-4,"yaw":0.5,"dayFraction":0.25})";
  SaveData d;
  CHECK(deserializeSave(v1, 1234, d) == SaveLoadResult::Migrated);
  CHECK_NEAR(d.hours, 6.0, 1e-5);
  CHECK_NEAR(d.playerPos.z, -4.0, 1e-6);
  CHECK(d.quality == 2);  // default settings
  // Re-saving writes the current version.
  CHECK(serializeSave(d).find("\"version\":4") != std::string::npos);
}

TEST_CASE("save: v3 saves from the vertical slice still load (v4 migration)") {
  // A real v3 save shape, written by the merged vertical slice (no masterVolume/fullscreen).
  const std::string v3 =
      R"({"format":"mistpine-save","version":3,"world":{"seed":1234,"hours":17.5,"playSeconds":321.5},)"
      R"("player":{"x":12.5,"y":40.25,"z":-88,"yaw":1.25},)"
      R"("settings":{"quality":1,"mouseSensitivity":1.4,"invertY":true},)"
      R"("survival":{"day":2,"health":90,"warmth":70,"satiety":60,"hydration":50,"wetness":0.25,)"
      R"("inventory":[3,1,2,0],"rest":{"x":1,"y":2,"z":3},"picked":[[7,123.5]],"fires":[[1,2,3,45]]}})";
  SaveData d;
  CHECK(deserializeSave(v3, 1234, d) == SaveLoadResult::Migrated);
  CHECK_NEAR(d.hours, 17.5, 1e-5);
  CHECK_NEAR(d.playerPos.x, 12.5, 1e-5);
  CHECK(d.quality == 1);
  CHECK_NEAR(d.mouseSensitivity, 1.4, 1e-6);
  CHECK(d.invertY);
  CHECK(d.day == 2);
  CHECK_NEAR(d.vitals.health, 90.0, 1e-4);
  CHECK(d.inventory[0] == 3 && d.inventory[3] == 0);
  CHECK(d.hasRestPoint);
  CHECK(d.picked.size() == 1 && d.picked[0].id == 7);
  CHECK(d.fires.size() == 1 && d.fires[0].fuel == 45.0f);
  // v4 defaults for the new settings.
  CHECK_NEAR(d.masterVolume, 1.0, 1e-6);
  CHECK(!d.fullscreen);
  // Re-saving writes v4 and keeps the migrated values.
  SaveData e;
  CHECK(deserializeSave(serializeSave(d), 1234, e) == SaveLoadResult::Ok);
  CHECK(e.quality == 1 && e.invertY && e.day == 2 && e.picked.size() == 1);
  CHECK_NEAR(e.masterVolume, 1.0, 1e-6);
}

TEST_CASE("save: v4 round-trips the desktop settings") {
  SaveData a = sample();
  a.masterVolume = 0.35f;
  a.fullscreen = true;
  SaveData b;
  CHECK(deserializeSave(serializeSave(a), 1234, b) == SaveLoadResult::Ok);
  CHECK_NEAR(b.masterVolume, 0.35, 1e-6);
  CHECK(b.fullscreen);
  // Out-of-range volume is rejected, not clamped.
  SaveData bad = sample();
  bad.masterVolume = 4.0f;
  CHECK(deserializeSave(serializeSave(bad), 1234, b) == SaveLoadResult::Invalid);
}

TEST_CASE("save: corrupt-save fuzz never crashes and always returns a defined result") {
  // Deterministic PRNG so failures are reproducible.
  uint32_t s = 0xC0FFEEu;
  auto rnd = [&s]() {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
  };
  const std::string valid = serializeSave(sample());
  int defined = 0, okOrMigrated = 0;
  for (int iter = 0; iter < 4000; ++iter) {
    std::string mutated = valid;
    const int mutations = 1 + static_cast<int>(rnd() % 8);
    for (int m = 0; m < mutations && !mutated.empty(); ++m) {
      const size_t pos = rnd() % mutated.size();
      switch (rnd() % 4) {
        case 0: mutated[pos] = static_cast<char>(rnd() % 256); break;   // bit/byte flip
        case 1: mutated.erase(pos, 1); break;                            // deletion
        case 2: mutated.insert(pos, 1, static_cast<char>(rnd() % 256)); break;  // insertion
        default: mutated.resize(pos); break;                             // truncation
      }
    }
    SaveData d;
    d.hours = 99.0f;  // sentinel: a defined result must not leave output half-written on failure
    const SaveLoadResult r = deserializeSave(mutated, 1234, d);
    switch (r) {
      case SaveLoadResult::Ok:
      case SaveLoadResult::Migrated:
      case SaveLoadResult::Empty:
      case SaveLoadResult::Corrupt:
      case SaveLoadResult::UnsupportedVersion:
      case SaveLoadResult::WrongWorld:
      case SaveLoadResult::Invalid:
        ++defined;
        if (r == SaveLoadResult::Ok || r == SaveLoadResult::Migrated) ++okOrMigrated;
        break;
    }
    // On failure the output must be untouched; on success it must be finite and in range.
    if (r == SaveLoadResult::Ok || r == SaveLoadResult::Migrated) {
      CHECK(std::isfinite(d.hours) && d.hours >= 0.0f && d.hours < 24.0f);
      CHECK(d.quality >= 0 && d.quality <= 2);
      CHECK(d.masterVolume >= 0.0f && d.masterVolume <= 1.0f);
    } else {
      CHECK(d.hours == 99.0f);
    }
  }
  CHECK(defined == 4000);        // every input produced a defined result (no crash, no UB path)
  CHECK(okOrMigrated >= 1);      // the unmutated shape still parses after all that
}

TEST_CASE("save: rejects corrupt, future, foreign and out-of-range data without touching output") {
  SaveData d;
  d.hours = 99.0f;  // sentinel
  CHECK(deserializeSave("", 1234, d) == SaveLoadResult::Empty);
  CHECK(deserializeSave("{\"version\":2,", 1234, d) == SaveLoadResult::Corrupt);
  CHECK(deserializeSave("not json", 1234, d) == SaveLoadResult::Corrupt);
  CHECK(deserializeSave(serializeSave(sample()) + "x", 1234, d) == SaveLoadResult::Corrupt);
  CHECK(deserializeSave(R"({"version":5})", 1234, d) == SaveLoadResult::UnsupportedVersion);
  CHECK(deserializeSave(serializeSave(sample()), 999, d) == SaveLoadResult::WrongWorld);
  SaveData bad = sample();
  bad.playerPos.x = 5000.0f;
  CHECK(deserializeSave(serializeSave(bad), 1234, d) == SaveLoadResult::Invalid);
  bad = sample();
  bad.hours = 24.5f;
  CHECK(deserializeSave(serializeSave(bad), 1234, d) == SaveLoadResult::Invalid);
  std::string deep(100, '{');
  CHECK(deserializeSave(deep, 1234, d) == SaveLoadResult::Corrupt);
  CHECK(d.hours == 99.0f);
}
