#include "world/terrain_generator.h"

#include <chrono>
#include <cmath>

#include "core/noise.h"

namespace aaa {
namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
constexpr float kBand = 48.0f;  // stream / trail distance fields are exact within this band
}  // namespace

float WorldFields::sample(const std::vector<float>& f, float x, float z) const {
  if (f.empty()) return 1e9f;
  const float fx = clampf((x + half) / cell, 0.0f, static_cast<float>(res - 1) - 0.001f);
  const float fz = clampf((z + half) / cell, 0.0f, static_cast<float>(res - 1) - 0.001f);
  const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
  const float tx = fx - ix, tz = fz - iz;
  const size_t i0 = static_cast<size_t>(iz) * res + ix;
  return lerp(lerp(f[i0], f[i0 + 1], tx), lerp(f[i0 + res], f[i0 + res + 1], tx), tz);
}

TerrainGenerator::TerrainGenerator(const WorldLayout& layout, int resolution)
    : layout_(layout), hf_(resolution, layout.worldSize) {
  fields_.cell = 2.0f;
  fields_.half = layout.worldSize * 0.5f;
  fields_.res = static_cast<int>(layout.worldSize / fields_.cell) + 1;
  const size_t n = static_cast<size_t>(fields_.res) * fields_.res;
  fields_.streamDist.assign(n, kBand);
  fields_.trailDist.assign(n, kBand);
  fields_.valleyDist.assign(n, 0.0f);
  fields_.valleyT.assign(n, 0.0f);
}

void TerrainGenerator::rasterizeBand(const Polyline& line, std::vector<float>& field, float band) {
  const int res = fields_.res;
  const float cell = fields_.cell, half = fields_.half;
  for (size_t i = 0; i + 1 < line.points.size(); ++i) {
    const Vec2 a = line.points[i], b = line.points[i + 1];
    const Vec2 ab = b - a;
    const float len2 = dot(ab, ab);
    const int x0 = std::max(0, static_cast<int>((std::fmin(a.x, b.x) - band + half) / cell));
    const int x1 = std::min(res - 1, static_cast<int>((std::fmax(a.x, b.x) + band + half) / cell) + 1);
    const int z0 = std::max(0, static_cast<int>((std::fmin(a.y, b.y) - band + half) / cell));
    const int z1 = std::min(res - 1, static_cast<int>((std::fmax(a.y, b.y) + band + half) / cell) + 1);
    for (int z = z0; z <= z1; ++z)
      for (int x = x0; x <= x1; ++x) {
        const Vec2 p{-half + x * cell, -half + z * cell};
        const float u = len2 > 0 ? clampf(dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const Vec2 q = a + ab * u;
        const float d = length(p - q);
        float& dst = field[static_cast<size_t>(z) * res + x];
        if (d < dst) dst = d;
      }
  }
}

float TerrainGenerator::shapeHeight(const WorldLayout& L, const WorldFields& F, float x, float z) {
  const uint32_t seed = L.seed;
  const float vd = F.valley(x, z);
  const float vt = F.valleyParam(x, z);
  const float sd = F.stream(x, z);
  const float td = F.trail(x, z);
  const float floorH = L.valleyFloorAt(vt);

  // Large-scale form: valley walls rising into ridged mountains.
  const float walls = 92.0f * std::pow(smoothstep(16.0f, 300.0f, vd), 1.3f);
  const float ridges = noise::ridged2(x / 240.0f + 3.1f, z / 240.0f - 1.7f, seed, 5);
  const float mountains = 165.0f * ridges * smoothstep(50.0f, 330.0f, vd);
  // Medium: rolling spurs and gullies on the slopes.
  const float warpX = 25.0f * noise::gradient2(x / 180.0f, z / 180.0f, seed + 11);
  const float warpZ = 25.0f * noise::gradient2(x / 180.0f + 7.7f, z / 180.0f, seed + 12);
  const float hills = 13.0f * noise::fbm2((x + warpX) / 95.0f, (z + warpZ) / 95.0f, seed + 1, 4) *
                      smoothstep(8.0f, 90.0f, vd);
  // Small: ground undulation (roots, mounds) — muted on paths and clearings.
  float detailMask = 1.0f;
  detailMask *= 0.35f + 0.65f * smoothstep(1.0f, 5.0f, td);
  for (const Clearing& c : L.clearings)
    detailMask *= 0.4f + 0.6f * smoothstep(c.radius * 0.6f, c.radius * 1.3f, length(Vec2{x, z} - c.center));
  const float detail = (1.7f * noise::fbm2(x / 17.0f, z / 17.0f, seed + 2, 3) +
                        0.35f * noise::fbm2(x / 4.5f, z / 4.5f, seed + 3, 2)) * detailMask;
  // World boundary: a natural ring of peaks.
  const float edgeT = std::fmax(std::fabs(x), std::fabs(z)) / (L.worldSize * 0.5f);
  const float edge = 185.0f * std::pow(smoothstep(0.78f, 1.0f, edgeT), 1.5f);

  float h = floorH + walls + mountains + hills + detail + edge;

  // Valley floor flattens toward the stream (alluvial terraces).
  const float bank = 1.0f - smoothstep(7.0f, 36.0f, sd);
  h = lerp(h, floorH + 1.3f + 0.6f * detail, bank * 0.88f);
  // Stream channel: smooth U-shaped bed.
  const float bed = 1.0f - smoothstep(1.2f, 6.5f, sd);
  h -= 2.5f * bed * bed * (3.0f - 2.0f * bed);

  // Shrine terrace: a level stone platform on a spur overlooking the valley.
  {
    const float d = length(Vec2{x, z} - L.shrine);
    const float target = L.valleyFloorAt(0.62f) + 30.0f;
    const float w = 1.0f - smoothstep(L.shrineTerraceRadius, L.shrineTerraceRadius + 34.0f, d);
    h = lerp(h, target + 0.25f * detail, w * w * (3.0f - 2.0f * w));
  }
  // Karst fields: knobbly limestone ground.
  for (const Clearing& k : L.karstFields) {
    const float d = length(Vec2{x, z} - k.center);
    const float w = 1.0f - smoothstep(k.radius * 0.5f, k.radius * 1.2f, d);
    if (w > 0.0f) h += w * 6.0f * noise::ridged2(x / 22.0f, z / 22.0f, seed + 40, 3);
  }
  return h;
}

bool TerrainGenerator::step(double budgetMs) {
  const auto t0 = Clock::now();
  const int fres = fields_.res;
  while (msSince(t0) < budgetMs) {
    switch (stage_) {
      case Stage::ValleyField: {
        // Distance to the valley line varies smoothly, so it is evaluated on a
        // coarse grid (every kStride-th sample) and bilinearly upsampled.
        constexpr int kStride = 4;
        const int cres = (fres - 1) / kStride + 1;
        if (coarseDist_.empty()) { coarseDist_.resize(static_cast<size_t>(cres) * cres); coarseT_.resize(coarseDist_.size()); }
        for (int cx = 0; cx < cres; ++cx) {
          const Vec2 p{-fields_.half + cx * kStride * fields_.cell, -fields_.half + row_ * kStride * fields_.cell};
          float t = 0;
          coarseDist_[static_cast<size_t>(row_) * cres + cx] = layout_.valley.distance(p, &t);
          coarseT_[static_cast<size_t>(row_) * cres + cx] = t;
        }
        if (++row_ >= cres) {
          for (int z = 0; z < fres; ++z)
            for (int x = 0; x < fres; ++x) {
              const int cx = std::min(x / kStride, cres - 2), cz = std::min(z / kStride, cres - 2);
              const float tx = static_cast<float>(x - cx * kStride) / kStride;
              const float tz = static_cast<float>(z - cz * kStride) / kStride;
              auto bl = [&](const std::vector<float>& c) {
                const size_t i0 = static_cast<size_t>(cz) * cres + cx;
                return lerp(lerp(c[i0], c[i0 + 1], tx), lerp(c[i0 + cres], c[i0 + cres + 1], tx), tz);
              };
              fields_.valleyDist[static_cast<size_t>(z) * fres + x] = bl(coarseDist_);
              fields_.valleyT[static_cast<size_t>(z) * fres + x] = bl(coarseT_);
            }
          coarseDist_.clear(); coarseDist_.shrink_to_fit();
          coarseT_.clear(); coarseT_.shrink_to_fit();
          stage_ = Stage::BandFields;
          row_ = 0;
        }
        break;
      }
      case Stage::BandFields:
        rasterizeBand(layout_.stream, fields_.streamDist, kBand);
        rasterizeBand(layout_.trail, fields_.trailDist, kBand);
        stage_ = Stage::Heights;
        break;
      case Stage::Heights: {
        const int res = hf_.resolution();
        for (int x = 0; x < res; ++x)
          hf_.at(x, row_) = shapeHeight(layout_, fields_, hf_.worldX(x), hf_.worldZ(row_));
        if (++row_ >= res) { stage_ = Stage::Finalize; row_ = 0; }
        break;
      }
      case Stage::Finalize:
        hf_.updateBounds();
        stage_ = Stage::Done;
        return true;
      case Stage::Done:
        return true;
    }
  }
  return stage_ == Stage::Done;
}

float TerrainGenerator::progress() const {
  switch (stage_) {
    case Stage::ValleyField: return 0.2f * row_ / ((fields_.res - 1) / 4 + 1);
    case Stage::BandFields: return 0.21f;
    case Stage::Heights: return 0.22f + 0.76f * row_ / hf_.resolution();
    case Stage::Finalize: return 0.99f;
    case Stage::Done: return 1.0f;
  }
  return 0.0f;
}

const char* TerrainGenerator::stageName() const {
  switch (stage_) {
    case Stage::ValleyField: return "Tracing the valley";
    case Stage::BandFields: return "Following the stream";
    case Stage::Heights: return "Raising the mountains";
    case Stage::Finalize:
    case Stage::Done: return "Settling the land";
  }
  return "";
}

}  // namespace aaa
