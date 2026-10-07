#pragma once
// World: terrain + layout + chunked, lazily generated scatter data.
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "world/heightfield.h"
#include "world/scatter.h"
#include "world/terrain_generator.h"
#include "world/world_layout.h"

namespace aaa {

struct WorldChunk {
  int cx = 0, cz = 0;
  bool generated = false;
  ScatterResult scatter;
  bool detailGenerated = false;       // ground cover (grass) near the player only
  std::vector<PropInstance> grass;
  uint32_t generation = 0;  // bumps when the content changes (renderer rebuilds caches)
};

class World {
 public:
  static constexpr float kChunkSize = 64.0f;

  explicit World(uint32_t seed, int heightfieldResolution = 1025);

  // Loading: call until it returns true.
  bool generateStep(double budgetMs);
  float loadProgress() const;
  const char* loadStage() const;
  bool ready() const { return ready_; }

  const WorldLayout& layout() const { return layout_; }
  const Heightfield& heightfield() const { return hf_; }
  const WorldFields& fields() const { return fields_; }

  int chunksPerSide() const { return chunksPerSide_; }
  WorldChunk* chunk(int cx, int cz);
  const WorldChunk* chunk(int cx, int cz) const;
  void chunkCoord(float x, float z, int& cx, int& cz) const;
  float chunkOriginX(int cx) const { return -hf_.halfSize() + cx * kChunkSize; }
  float chunkOriginZ(int cz) const { return -hf_.halfSize() + cz * kChunkSize; }

  // Streaming: ensures scatter data exists within `radius` of `pos` (at most
  // `maxNew` chunks per call) and releases chunks beyond `releaseRadius`.
  void streamAround(Vec3 pos, float radius, float releaseRadius, int maxNew);
  void streamDetailAround(Vec3 pos, float radius, float releaseRadius, int maxNew);
  int detailChunkCount() const;
  int generatedChunkCount() const;

  float groundHeight(float x, float z) const { return hf_.height(x, z); }
  // Water depth of the stream at (x,z); 0 when dry.
  float waterDepth(float x, float z) const;
  float waterSurface(float x, float z) const;

  void forEachColliderNear(Vec2 p, float radius, const std::function<void(const CircleCollider&)>& fn) const;

 private:
  void generateChunk(WorldChunk& c);

  WorldLayout layout_;
  std::unique_ptr<TerrainGenerator> generator_;
  Heightfield hf_;
  WorldFields fields_;
  int chunksPerSide_ = 0;
  std::vector<WorldChunk> chunks_;
  bool ready_ = false;
};

}  // namespace aaa
