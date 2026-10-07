#pragma once
// Authored world composition. The *structure* (valley, stream, landmarks, paths)
// is designed by hand; seeded noise only perturbs it. This avoids the
// "flat terrain + random trees" look and keeps the world deterministic.
#include <cstdint>
#include <vector>

#include "core/math.h"

namespace aaa {

struct Polyline {
  std::vector<Vec2> points;   // dense, roughly uniform spacing
  std::vector<float> cumLen;  // cumulative length per point

  float totalLength() const { return cumLen.empty() ? 0.0f : cumLen.back(); }
  // Closest distance to the polyline; outT receives the normalised arc parameter [0,1].
  float distance(Vec2 p, float* outT = nullptr) const;
  Vec2 pointAt(float t) const;  // t in [0,1]
};

// Catmull-Rom resampling of control points at roughly `spacing` metres.
Polyline makeSpline(const std::vector<Vec2>& control, float spacing);

struct Clearing {
  Vec2 center;
  float radius;
};

struct WorldLayout {
  uint32_t seed = 0;
  float worldSize = 1024.0f;

  Polyline valley;      // valley floor centreline (smooth)
  Polyline stream;      // stream bed (meanders around the valley line)
  Polyline trail;       // faint animal trail from the spawn clearing up to the shrine terrace

  float valleyFloorLow = 18.0f;   // height at the valley mouth (t = 0)
  float valleyFloorHigh = 64.0f;  // height at the valley head (t = 1)

  Vec2 spawn;
  Clearing spawnClearing;
  Vec2 shrine;           // landmark: abandoned shrine on a terrace
  float shrineTerraceRadius = 26.0f;
  float shrineTerraceHeight = 0.0f;  // filled by the generator
  std::vector<Clearing> clearings;
  std::vector<Clearing> bambooGroves;
  std::vector<Clearing> karstFields;

  float valleyFloorAt(float t) const { return lerp(valleyFloorLow, valleyFloorHigh, t); }
};

WorldLayout makeWorldLayout(uint32_t seed);

}  // namespace aaa
