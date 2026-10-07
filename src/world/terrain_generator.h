#pragma once
// Incremental terrain generation. The browser must never freeze for long, so the
// work is split into small steps that the loading screen advances each frame.
#include <cstdint>
#include <memory>
#include <vector>

#include "world/heightfield.h"
#include "world/world_layout.h"

namespace aaa {

// Coarse scalar fields derived from the layout, reused by scatter rules.
struct WorldFields {
  int res = 0;          // samples per side
  float cell = 2.0f;    // metres per sample
  float half = 512.0f;
  std::vector<float> streamDist;  // metres to stream centreline (capped at band)
  std::vector<float> trailDist;   // metres to trail centreline (capped at band)
  std::vector<float> valleyDist;  // metres to valley centreline
  std::vector<float> valleyT;     // arc parameter of nearest valley point [0,1]

  float sample(const std::vector<float>& f, float x, float z) const;
  float stream(float x, float z) const { return sample(streamDist, x, z); }
  float trail(float x, float z) const { return sample(trailDist, x, z); }
  float valley(float x, float z) const { return sample(valleyDist, x, z); }
  float valleyParam(float x, float z) const { return sample(valleyT, x, z); }
};

class TerrainGenerator {
 public:
  TerrainGenerator(const WorldLayout& layout, int resolution);

  // Advances generation by roughly `budgetMs` of work. Returns true when done.
  bool step(double budgetMs);
  float progress() const;
  const char* stageName() const;

  Heightfield takeHeightfield() { return std::move(hf_); }
  WorldFields takeFields() { return std::move(fields_); }

  // Pure function of the layout; exposed for tests.
  static float shapeHeight(const WorldLayout& L, const WorldFields& F, float x, float z);

 private:
  enum class Stage { ValleyField, BandFields, Heights, Finalize, Done };
  void rasterizeBand(const Polyline& line, std::vector<float>& field, float band);

  const WorldLayout& layout_;
  Heightfield hf_;
  WorldFields fields_;
  std::vector<float> coarseDist_, coarseT_;
  Stage stage_ = Stage::ValleyField;
  int row_ = 0;
};

}  // namespace aaa
