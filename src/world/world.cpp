#include "world/world.h"

#include <algorithm>
#include <cmath>

namespace aaa {

World::World(uint32_t seed, int heightfieldResolution) : layout_(makeWorldLayout(seed)) {
  generator_ = std::make_unique<TerrainGenerator>(layout_, heightfieldResolution);
  chunksPerSide_ = static_cast<int>(layout_.worldSize / kChunkSize);
  chunks_.resize(static_cast<size_t>(chunksPerSide_) * chunksPerSide_);
  for (int z = 0; z < chunksPerSide_; ++z)
    for (int x = 0; x < chunksPerSide_; ++x) {
      chunks_[static_cast<size_t>(z) * chunksPerSide_ + x].cx = x;
      chunks_[static_cast<size_t>(z) * chunksPerSide_ + x].cz = z;
    }
}

bool World::generateStep(double budgetMs) {
  if (ready_) return true;
  if (generator_->step(budgetMs)) {
    hf_ = generator_->takeHeightfield();
    fields_ = generator_->takeFields();
    generator_.reset();
    layout_.shrineTerraceHeight = hf_.height(layout_.shrine.x, layout_.shrine.y);
    shrine_ = makeShrineLayout(layout_, hf_);
    ready_ = true;
  }
  return ready_;
}

float World::loadProgress() const { return ready_ ? 1.0f : generator_->progress(); }
const char* World::loadStage() const { return ready_ ? "Ready" : generator_->stageName(); }

WorldChunk* World::chunk(int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= chunksPerSide_ || cz >= chunksPerSide_) return nullptr;
  return &chunks_[static_cast<size_t>(cz) * chunksPerSide_ + cx];
}
const WorldChunk* World::chunk(int cx, int cz) const { return const_cast<World*>(this)->chunk(cx, cz); }

void World::chunkCoord(float x, float z, int& cx, int& cz) const {
  cx = static_cast<int>(std::floor((x + hf_.halfSize()) / kChunkSize));
  cz = static_cast<int>(std::floor((z + hf_.halfSize()) / kChunkSize));
}

void World::generateChunk(WorldChunk& c) {
  ScatterContext ctx{&layout_, &hf_, &fields_};
  c.scatter = ScatterResult{};
  scatterRegion(ctx, chunkOriginX(c.cx), chunkOriginZ(c.cz), kChunkSize, c.scatter);
  c.generated = true;
  ++c.generation;
}

void World::streamAround(Vec3 pos, float radius, float releaseRadius, int maxNew) {
  if (!ready_) return;
  int created = 0;
  // Nearest-first: visit chunks by distance from the player.
  struct Cand { float d2; WorldChunk* c; };
  std::vector<Cand> wanted;
  for (WorldChunk& c : chunks_) {
    const float cxw = chunkOriginX(c.cx) + kChunkSize * 0.5f, czw = chunkOriginZ(c.cz) + kChunkSize * 0.5f;
    const float dx = std::fmax(std::fabs(pos.x - cxw) - kChunkSize * 0.5f, 0.0f);
    const float dz = std::fmax(std::fabs(pos.z - czw) - kChunkSize * 0.5f, 0.0f);
    const float d2 = dx * dx + dz * dz;
    if (!c.generated && d2 <= radius * radius) wanted.push_back({d2, &c});
    else if (c.generated && d2 > releaseRadius * releaseRadius) {
      c.scatter = ScatterResult{};
      c.generated = false;
      ++c.generation;
    }
  }
  std::sort(wanted.begin(), wanted.end(), [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
  for (Cand& w : wanted) {
    if (created >= maxNew) break;
    generateChunk(*w.c);
    ++created;
  }
}

void World::streamDetailAround(Vec3 pos, float radius, float releaseRadius, int maxNew) {
  if (!ready_) return;
  int created = 0;
  ScatterContext ctx{&layout_, &hf_, &fields_};
  for (WorldChunk& c : chunks_) {
    const float cxw = chunkOriginX(c.cx) + kChunkSize * 0.5f, czw = chunkOriginZ(c.cz) + kChunkSize * 0.5f;
    const float dx = std::fmax(std::fabs(pos.x - cxw) - kChunkSize * 0.5f, 0.0f);
    const float dz = std::fmax(std::fabs(pos.z - czw) - kChunkSize * 0.5f, 0.0f);
    const float d2 = dx * dx + dz * dz;
    if (!c.detailGenerated && d2 <= radius * radius && created < maxNew) {
      ScatterResult r;
      scatterRegion(ctx, chunkOriginX(c.cx), chunkOriginZ(c.cz), kChunkSize, r, kGrassOnly);
      c.grass = std::move(r.instances[static_cast<int>(PropKind::Grass)]);
      c.detailGenerated = true;
      ++c.generation;
      ++created;
    } else if (c.detailGenerated && d2 > releaseRadius * releaseRadius) {
      c.grass.clear();
      c.grass.shrink_to_fit();
      c.detailGenerated = false;
      ++c.generation;
    }
  }
}

int World::detailChunkCount() const {
  int n = 0;
  for (const WorldChunk& c : chunks_) n += c.detailGenerated ? 1 : 0;
  return n;
}

int World::generatedChunkCount() const {
  int n = 0;
  for (const WorldChunk& c : chunks_) n += c.generated ? 1 : 0;
  return n;
}

float World::waterSurface(float x, float z) const {
  // Stream surface sits a little below the alluvial bank height.
  return layout_.valleyFloorAt(fields_.valleyParam(x, z)) + 0.55f;
}

float World::waterDepth(float x, float z) const {
  if (fields_.stream(x, z) > 8.0f) return 0.0f;
  return std::fmax(0.0f, waterSurface(x, z) - hf_.height(x, z));
}

float World::slope(float x, float z) const {
  const float e = 1.0f;
  const float dx = hf_.height(x + e, z) - hf_.height(x - e, z);
  const float dz = hf_.height(x, z + e) - hf_.height(x, z - e);
  const Vec3 n = normalize(Vec3{-dx, 2.0f * e, -dz});
  return 1.0f - n.y;
}

void World::forEachColliderNear(Vec2 p, float radius, const std::function<void(const CircleCollider&)>& fn) const {
  if (ready_ && length(p - shrine_.center) < 40.0f + radius) {
    for (const CircleCollider& col : shrine_.colliders) {
      const Vec2 d = col.center - p;
      const float r = col.radius + radius;
      if (dot(d, d) <= r * r) fn(col);
    }
  }
  int cx0, cz0, cx1, cz1;
  chunkCoord(p.x - radius - 8.0f, p.y - radius - 8.0f, cx0, cz0);
  chunkCoord(p.x + radius + 8.0f, p.y + radius + 8.0f, cx1, cz1);
  for (int cz = cz0; cz <= cz1; ++cz)
    for (int cx = cx0; cx <= cx1; ++cx) {
      const WorldChunk* c = chunk(cx, cz);
      if (!c || !c->generated) continue;
      for (const CircleCollider& col : c->scatter.colliders) {
        const Vec2 d = col.center - p;
        const float r = col.radius + radius;
        if (dot(d, d) <= r * r) fn(col);
      }
    }
}

}  // namespace aaa
