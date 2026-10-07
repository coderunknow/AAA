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
  CHECK(serializeSave(d).find("\"version\":3") != std::string::npos);
}

TEST_CASE("save: rejects corrupt, future, foreign and out-of-range data without touching output") {
  SaveData d;
  d.hours = 99.0f;  // sentinel
  CHECK(deserializeSave("", 1234, d) == SaveLoadResult::Empty);
  CHECK(deserializeSave("{\"version\":2,", 1234, d) == SaveLoadResult::Corrupt);
  CHECK(deserializeSave("not json", 1234, d) == SaveLoadResult::Corrupt);
  CHECK(deserializeSave(serializeSave(sample()) + "x", 1234, d) == SaveLoadResult::Corrupt);
  CHECK(deserializeSave(R"({"version":4})", 1234, d) == SaveLoadResult::UnsupportedVersion);
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
