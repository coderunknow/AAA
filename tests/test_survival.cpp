// Survival loop, interactables, wildlife, shrine and save v3.
#include <cmath>
#include <set>

#include "game/game.h"
#include "game/wolf_pose.h"
#include "procgen/structure_meshes.h"
#include "test.h"

using namespace aaa;

namespace {
Game& loadedGame() {
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

void step(Game& g, int frames, const InputFrame& in = {}) {
  for (int i = 0; i < frames; ++i) g.update(1.0f / 60.0f, in, 0.0);
}
}  // namespace

TEST_CASE("survival: night cold drains warmth; a fire restores it") {
  Survival s;
  SurvivalInputs in;
  in.dt = 1.0f;
  in.airTemperature = Survival::airTemperature(3.0f, 30.0f);
  CHECK(in.airTemperature < 3.0f);
  for (int i = 0; i < 120; ++i) s.update(in);
  const float cold = s.vitals().warmth;
  CHECK(cold < 75.0f);
  in.fireHeat = 1.0f;
  for (int i = 0; i < 20; ++i) s.update(in);
  CHECK(s.vitals().warmth > cold + 40.0f);
}

TEST_CASE("survival: mid-morning is comfortable, wading soaks and chills") {
  Survival s;
  SurvivalInputs in;
  in.dt = 1.0f;
  in.airTemperature = Survival::airTemperature(10.0f, 30.0f);
  for (int i = 0; i < 60; ++i) s.update(in);
  CHECK(s.vitals().warmth >= 99.0f);
  in.waterDepth = 0.6f;
  for (int i = 0; i < 10; ++i) s.update(in);
  CHECK(s.vitals().wetness > 0.9f);
  CHECK(s.feltTemperature() < in.airTemperature - 5.0f);
}

TEST_CASE("survival: needs drain, zero needs hurt, collapse is reported once with a cause") {
  Survival s;
  Vitals v;
  v.hydration = 0.5f;
  v.health = 2.0f;
  s.restore(v);
  SurvivalInputs in;
  in.dt = 1.0f;
  in.airTemperature = 12.0f;
  for (int i = 0; i < 30 && !s.collapsed(); ++i) s.update(in);
  CHECK(s.collapsed());
  CHECK(s.lastDamage() == DamageCause::Thirst);
  s.reviveAfterCollapse();
  CHECK(!s.collapsed());
  CHECK(s.vitals().health > 30.0f);
  CHECK(s.vitals().hydration >= 30.0f);
}

TEST_CASE("survival: falls hurt only above the safe landing speed") {
  Survival s;
  SurvivalInputs in;
  in.dt = 0.016f;
  in.landingSpeed = 8.0f;
  s.update(in);
  CHECK(s.vitals().health == 100.0f);
  in.landingSpeed = 14.0f;
  s.update(in);
  CHECK(s.vitals().health < 60.0f);
  CHECK(s.lastDamage() == DamageCause::Fall);
}

TEST_CASE("inventory: capacity is enforced and take fails without enough items") {
  Inventory inv;
  CHECK(inv.add(ItemKind::Flint, 10) == Inventory::capacity(ItemKind::Flint));
  CHECK(inv.add(ItemKind::Flint, 1) == 0);
  CHECK(!inv.take(ItemKind::Branch, 1));
  CHECK(inv.take(ItemKind::Flint, 2));
  CHECK(inv.get(ItemKind::Flint) == Inventory::capacity(ItemKind::Flint) - 2);
}

TEST_CASE("campfire: heat falls off with distance and fire intensity ramps up") {
  std::vector<Campfire> fires{{{0, 0, 0}, Campfire::kStartFuel, 10.0f}};
  CHECK(fireHeatAt(fires, {0.5f, 0, 0}) > 0.9f);
  CHECK(fireHeatAt(fires, {Campfire::kWarmRadius + 0.1f, 0, 0}) == 0.0f);
  fires[0].age = 0.0f;
  CHECK(fires[0].intensity() == 0.0f);
  fires[0].fuel = 0.0f;
  fires[0].age = 10.0f;
  CHECK(fireHeatAt(fires, {0.5f, 0, 0}) == 0.0f);
}

TEST_CASE("pickups: deterministic per seed, unique ids, starter cache near spawn, valid ground") {
  Game& g = loadedGame();
  const std::vector<Pickup> a = generatePickups(g.world());
  const std::vector<Pickup> b = generatePickups(g.world());
  CHECK(a.size() > 150);
  CHECK(a.size() == b.size());
  std::set<int> ids;
  int nearSpawn[4] = {0, 0, 0, 0};
  const Vec2 sp = g.world().layout().spawn;
  for (size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].id == b[i].id && a[i].pos.x == b[i].pos.x && a[i].pos.z == b[i].pos.z);
    ids.insert(a[i].id);
    CHECK(g.world().waterDepth(a[i].pos.x, a[i].pos.z) == 0.0f);
    if (length(a[i].pos.xz() - sp) < 30.0f) ++nearSpawn[static_cast<int>(a[i].kind)];
  }
  CHECK(ids.size() == a.size());
  CHECK(nearSpawn[static_cast<int>(PickupKind::Deadwood)] >= 2);
  CHECK(nearSpawn[static_cast<int>(PickupKind::FlintStone)] >= 1);
}

TEST_CASE("shrine: platform is standable, colliders block pillars, rest point is on the platform") {
  Game& g = loadedGame();
  const ShrineLayout& sh = g.world().shrine();
  const Vec3 c = sh.toWorld({0.0f, 0.0f, 0.0f});
  CHECK_NEAR(g.world().groundHeight(c.x, c.z), sh.platformTop, 1e-3);
  CHECK(g.world().groundHeight(sh.restPoint.x, sh.restPoint.z) >= sh.platformTop - 1e-3f);
  // Local/world round trip.
  const Vec2 l = sh.toLocal(sh.toWorld({1.5f, 0.0f, -2.0f}).xz());
  CHECK_NEAR(l.x, 1.5, 1e-3);
  CHECK_NEAR(l.y, -2.0, 1e-3);
  int hits = 0;
  const Vec3 pillar = sh.toWorld({-3.6f, 0.0f, -2.8f});
  g.world().forEachColliderNear(pillar.xz(), 0.1f, [&](const CircleCollider&) { ++hits; });
  CHECK(hits >= 1);
  CHECK(sh.inSanctuary(sh.center));
}

TEST_CASE("structure meshes: shrine, pickups and campfire are non-empty with finite unit normals") {
  Game& g = loadedGame();
  const auto shrine = procgen::buildShrineMeshes(g.world().shrine(), g.world().heightfield());
  CHECK(shrine.size() >= 4);
  size_t tris = 0;
  for (const auto& m : shrine) {
    tris += m.mesh.triangleCount();
    // Only vertices used by triangles (the collapsed roof corner leaves orphans behind its holes).
    int bad = 0;
    for (uint32_t idx : m.mesh.indices) {
      const procgen::Vertex& v = m.mesh.vertices[idx];
      const float n = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
      bad += (!std::isfinite(v.px) || std::fabs(n - 1.0f) > 0.02f) ? 1 : 0;
    }
    CHECK(bad == 0);
    if (bad) std::printf("    material %d: %d bad normals\n", static_cast<int>(m.material), bad);
  }
  CHECK(tris > 2000);
  for (int k = 0; k < 4; ++k)
    for (int v = 0; v < 4; ++v) CHECK(!procgen::buildPickupMesh(k, v).empty());
  CHECK(procgen::buildCampfireMesh().size() == 4);
  // Box faces point outward: the top face of a unit box has +Y normals.
  procgen::MeshData box;
  procgen::addBox(box, Mat4::scale({2, 1, 3}));
  bool topUp = false;
  for (const procgen::Vertex& v : box.vertices) topUp |= (v.py > 0.99f && v.ny > 0.99f);
  CHECK(topUp);
  CHECK(box.triangleCount() == 12);
}

TEST_CASE("wildlife: wolves stalk a lone player at night, flee from fire, avoid the shrine") {
  Game& g = loadedGame();
  Wildlife w;
  w.init(g.world(), 7u);
  CHECK(w.wolves().size() == 3);
  std::vector<WildlifeEvent> ev;
  WildlifeContext ctx;
  ctx.dt = 0.1f;
  ctx.daylight = 0.0f;
  ctx.player = w.wolves()[0].pos + Vec3{20.0f, 0.0f, 0.0f};
  std::vector<Campfire> fires;
  ctx.fires = &fires;
  bool stalked = false;
  for (int i = 0; i < 100 && !stalked; ++i) {
    w.update(ctx, g.world(), ev);
    for (const Wolf& wf : w.wolves()) stalked |= wf.state == WolfState::Stalk;
  }
  CHECK(stalked);
  fires.push_back({ctx.player, Campfire::kStartFuel, 10.0f});
  bool allBack = false;
  for (int i = 0; i < 50 && !allBack; ++i) {
    w.update(ctx, g.world(), ev);
    allBack = true;
    for (const Wolf& wf : w.wolves()) allBack &= wf.state == WolfState::Retreat || wf.state == WolfState::Roam;
  }
  CHECK(allBack);
  const Vec2 shrine = g.world().shrine().center;
  for (const Wolf& wf : w.wolves()) CHECK(length(wf.pos.xz() - shrine) >= 15.9f);
  WolfPose pose;
  buildWolfPose(w.wolves()[0], 1.0f, pose);
  for (const PartPose& p : pose) {
    const Vec3 pos = p.transform.position();
    CHECK(std::isfinite(pos.x) && length(pos - w.wolves()[0].pos) < 2.0f);
  }
}

TEST_CASE("game: gather, build a fire, warm up, and the state survives a save round trip") {
  Game& g = loadedGame();
  g.events().clear();
  // Walk onto the nearest deadwood pile and gather it.
  const Pickup* wood = nullptr;
  for (const Pickup& p : g.pickups())
    if (p.kind == PickupKind::Deadwood && p.available) { wood = &p; break; }
  CHECK(wood != nullptr);
  if (!wood) return;
  const uint16_t woodId = wood->id;
  g.placePlayerForTest(wood->pos + Vec3{0.0f, 0.0f, -0.8f}, 0.0f);
  step(g, 2);
  CHECK(g.prompt().find("branches") != std::string::npos);
  InputFrame press;
  press.interact = true;
  step(g, 1, press);
  CHECK(g.inventory().get(ItemKind::Branch) >= 2);
  bool gotEvent = false;
  for (const GameEvent& e : g.events()) gotEvent |= e.type == GameEvent::Pickup;
  CHECK(gotEvent);

  // Build a fire on open ground (test hook supplies materials).
  g.inventoryForTest().add(ItemKind::Branch, 4);
  g.inventoryForTest().add(ItemKind::Flint, 1);
  const size_t firesBefore = g.campfires().size();
  // Find flat, dry ground away from colliders near the spawn.
  const Vec2 sp = g.world().layout().spawn;
  bool built = false;
  for (int i = 0; i < 40 && !built; ++i) {
    const float a = i * 0.7f, d = 4.0f + i * 0.5f;
    const Vec3 p{sp.x + std::sin(a) * d, 0.0f, sp.y + std::cos(a) * d};
    g.placePlayerForTest({p.x, g.world().groundHeight(p.x, p.z), p.z}, a);
    InputFrame f;
    f.buildFire = true;
    step(g, 1, f);
    built = g.campfires().size() > firesBefore;
  }
  CHECK(built);
  if (!built) return;
  Vitals cold = g.survival().vitals();
  cold.warmth = 20.0f;
  g.survivalForTest().restore(cold);
  const Campfire fire = g.campfires().back();
  g.placePlayerForTest(fire.pos + Vec3{1.2f, 0.0f, 0.0f}, 0.0f);
  step(g, 240);
  CHECK(g.fireHeat() > 0.3f);
  CHECK(g.survival().vitals().warmth > 30.0f);

  // Save round trip keeps picked spots, inventory, fires and vitals.
  const SaveData saved = g.makeSave();
  SaveData back;
  CHECK(deserializeSave(serializeSave(saved), saved.worldSeed, back) == SaveLoadResult::Ok);
  CHECK(back.inventory[0] == g.inventory().get(ItemKind::Branch));
  CHECK(back.fires.size() == saved.fires.size() && !back.fires.empty());
  bool hasWood = false;
  for (const SavedPickup& p : back.picked) hasWood |= p.id == woodId;
  CHECK(hasWood);
  CHECK_NEAR(back.vitals.warmth, saved.vitals.warmth, 1e-3);
}

TEST_CASE("save: v2 saves migrate to v3 with fresh survival state") {
  const std::string v2 =
      R"({"format":"mistpine-save","version":2,"world":{"seed":1234,"hours":9,"playSeconds":50},)"
      R"("player":{"x":1,"y":20,"z":2,"yaw":0}})";
  SaveData d;
  CHECK(deserializeSave(v2, 1234, d) == SaveLoadResult::Migrated);
  CHECK(d.day == 1);
  CHECK(d.vitals.health == 100.0f);
  CHECK(d.picked.empty() && d.fires.empty());
}

TEST_CASE("save: v3 rejects out-of-range survival data and malformed arrays") {
  SaveData d;
  d.worldSeed = 1234;
  d.vitals.health = 150.0f;
  SaveData out;
  CHECK(deserializeSave(serializeSave(d), 1234, out) == SaveLoadResult::Invalid);
  d.vitals.health = 50.0f;
  d.fires.push_back({{9000.0f, 0.0f, 0.0f}, 10.0f});
  CHECK(deserializeSave(serializeSave(d), 1234, out) == SaveLoadResult::Invalid);
  d.fires.clear();
  std::string text = serializeSave(d);
  const size_t at = text.find("\"inventory\":[");
  CHECK(at != std::string::npos);
  text.replace(at, 13, "\"inventory\":[1,");  // five entries
  CHECK(deserializeSave(text, 1234, out) == SaveLoadResult::Corrupt);
}
