#include "test.h"
#include "world/scatter.h"
#include "world/world.h"

using namespace aaa;

namespace {
World& sharedWorld() {
  static World* w = [] {
    auto* world = new World(1337u, 513);  // reduced resolution keeps tests fast
    while (!world->generateStep(1000.0)) {}
    return world;
  }();
  return *w;
}
}  // namespace

TEST_CASE("world: generation completes with plausible relief") {
  World& w = sharedWorld();
  CHECK(w.ready());
  const Heightfield& hf = w.heightfield();
  CHECK(hf.maxHeight() - hf.minHeight() > 120.0f);  // real mountains, not flat terrain
  CHECK(hf.minHeight() > -10.0f);
}

TEST_CASE("world: stream channel is lower than its banks and holds water") {
  World& w = sharedWorld();
  const Polyline& s = w.layout().stream;
  int wet = 0, checked = 0;
  for (float t = 0.1f; t < 0.9f; t += 0.05f) {
    const Vec2 p = s.pointAt(t);
    ++checked;
    if (w.waterDepth(p.x, p.y) > 0.2f) ++wet;
  }
  CHECK(wet >= checked * 3 / 4);
}

TEST_CASE("world: spawn is dry, gentle and on the map") {
  World& w = sharedWorld();
  const Vec2 sp = w.layout().spawn;
  CHECK(w.heightfield().contains(sp.x, sp.y));
  CHECK(w.waterDepth(sp.x, sp.y) == 0.0f);
  CHECK(w.heightfield().slope(sp.x, sp.y) < 0.15f);
}

TEST_CASE("world: shrine terrace is elevated above the valley floor") {
  World& w = sharedWorld();
  const Vec2 sh = w.layout().shrine;
  const float terrace = w.heightfield().height(sh.x, sh.y);
  const Vec2 sp = w.layout().spawn;
  CHECK(terrace > w.heightfield().height(sp.x, sp.y) + 15.0f);
  CHECK(w.heightfield().slope(sh.x, sh.y) < 0.08f);  // level platform
}

TEST_CASE("scatter: deterministic and independent of generation order") {
  World& w = sharedWorld();
  ScatterContext ctx{&w.layout(), &w.heightfield(), &w.fields()};
  ScatterResult a, b, whole;
  scatterRegion(ctx, -128, -128, 64, a);
  scatterRegion(ctx, -128, -128, 64, b);
  for (int k = 0; k < kPropKindCount; ++k) {
    CHECK(a.instances[k].size() == b.instances[k].size());
    for (size_t i = 0; i < a.instances[k].size() && i < b.instances[k].size(); ++i)
      CHECK(a.instances[k][i].position.x == b.instances[k][i].position.x);
  }
  // Two halves == one whole (no seams / double placement at chunk borders).
  ScatterResult left, right;
  scatterRegion(ctx, -128, -128, 32, left);
  scatterRegion(ctx, -96, -128, 32, right);
  // whole column strip [-128,-64) x [-128,-96) vs left+right restricted to same z range
  scatterRegion(ctx, -128, -128, 64, whole);
  auto countIn = [](const ScatterResult& r, int k, float z0, float z1) {
    int n = 0;
    for (auto& i : r.instances[k]) if (i.position.z >= z0 && i.position.z < z1) ++n;
    return n;
  };
  const int k = static_cast<int>(PropKind::Pine);
  CHECK(countIn(left, k, -128, -96) + countIn(right, k, -128, -96) == countIn(whole, k, -128, -96));
}

TEST_CASE("scatter: composition rules (no trees in the stream, bamboo in groves)") {
  World& w = sharedWorld();
  ScatterContext ctx{&w.layout(), &w.heightfield(), &w.fields()};
  const Vec2 sp = w.layout().stream.pointAt(0.5f);
  CHECK(propDensity(ctx, PropKind::Pine, sp.x, sp.y) == 0.0f);
  const Clearing& g = w.layout().bambooGroves[0];
  float best = 0.0f;
  for (int i = 0; i < 16; ++i) {
    const float a = i * 0.39f;
    best = std::fmax(best, propDensity(ctx, PropKind::Bamboo, g.center.x + std::cos(a) * 8, g.center.y + std::sin(a) * 8));
  }
  CHECK(best > 0.3f);
}

TEST_CASE("world: streaming loads near chunks and releases far ones") {
  World& w = sharedWorld();
  const Vec2 sp = w.layout().spawn;
  w.streamAround({sp.x, 0, sp.y}, 150.0f, 220.0f, 1000);
  const int near = w.generatedChunkCount();
  CHECK(near > 4);
  w.streamAround({-sp.x, 0, -sp.y}, 150.0f, 220.0f, 1000);
  int cx, cz;
  w.chunkCoord(sp.x, sp.y, cx, cz);
  CHECK(!w.chunk(cx, cz)->generated);  // released once far away
}

TEST_CASE("heightfield: raycast hits the ground") {
  World& w = sharedWorld();
  const Vec2 sp = w.layout().spawn;
  const float g = w.groundHeight(sp.x, sp.y);
  float dist = 0;
  CHECK(w.heightfield().raycast({sp.x, g + 10.0f, sp.y}, {0, -1, 0}, 50.0f, dist));
  CHECK_NEAR(dist, 10.0, 0.3);
}
