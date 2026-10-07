#include "core/noise.h"

#include <cmath>

#include "core/math.h"
#include "core/rng.h"

namespace aaa::noise {
namespace {

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// 16 evenly spaced unit gradients: avoids per-sample trig, identical on all platforms.
constexpr float kGrad16[16][2] = {
    {1.0f, 0.0f}, {0.92388f, 0.38268f}, {0.70711f, 0.70711f}, {0.38268f, 0.92388f},
    {0.0f, 1.0f}, {-0.38268f, 0.92388f}, {-0.70711f, 0.70711f}, {-0.92388f, 0.38268f},
    {-1.0f, 0.0f}, {-0.92388f, -0.38268f}, {-0.70711f, -0.70711f}, {-0.38268f, -0.92388f},
    {0.0f, -1.0f}, {0.38268f, -0.92388f}, {0.70711f, -0.70711f}, {0.92388f, -0.38268f}};

inline float grad2(int ix, int iy, uint32_t seed, float dx, float dy) {
  const float* g = kGrad16[hash2i(ix, iy, seed) & 15u];
  return g[0] * dx + g[1] * dy;
}

inline float grad3(int ix, int iy, int iz, uint32_t seed, float dx, float dy, float dz) {
  const uint32_t h = hashCombine(hash2i(ix, iy, seed), static_cast<uint32_t>(iz)) & 15u;
  // Ken Perlin's 12 edge gradients (+4 repeats).
  const float u = h < 8 ? dx : dy;
  const float v = h < 4 ? dy : (h == 12 || h == 14 ? dx : dz);
  return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}

}  // namespace

float gradient2(float x, float y, uint32_t seed) {
  const float fx = std::floor(x), fy = std::floor(y);
  const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
  const float dx = x - fx, dy = y - fy;
  const float u = fade(dx), v = fade(dy);
  const float n00 = grad2(ix, iy, seed, dx, dy);
  const float n10 = grad2(ix + 1, iy, seed, dx - 1, dy);
  const float n01 = grad2(ix, iy + 1, seed, dx, dy - 1);
  const float n11 = grad2(ix + 1, iy + 1, seed, dx - 1, dy - 1);
  return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * 1.4142f;
}

float gradient3(float x, float y, float z, uint32_t seed) {
  const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
  const float dx = x - fx, dy = y - fy, dz = z - fz;
  const float u = fade(dx), v = fade(dy), w = fade(dz);
  auto g = [&](int ox, int oy, int oz) { return grad3(ix + ox, iy + oy, iz + oz, seed, dx - ox, dy - oy, dz - oz); };
  const float x00 = lerp(g(0, 0, 0), g(1, 0, 0), u), x10 = lerp(g(0, 1, 0), g(1, 1, 0), u);
  const float x01 = lerp(g(0, 0, 1), g(1, 0, 1), u), x11 = lerp(g(0, 1, 1), g(1, 1, 1), u);
  return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

float fbm2(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain) {
  float sum = 0, amp = 1, norm = 0;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * gradient2(x, y, seed + static_cast<uint32_t>(i) * 1013u);
    norm += amp;
    x *= lacunarity; y *= lacunarity;
    amp *= gain;
  }
  return sum / norm;
}

float ridged2(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain) {
  float sum = 0, amp = 0.5f, weight = 1.0f, norm = 0;
  for (int i = 0; i < octaves; ++i) {
    float n = 1.0f - std::fabs(gradient2(x, y, seed + static_cast<uint32_t>(i) * 7919u));
    n *= n;
    n *= weight;
    weight = saturate(n * 2.0f);
    sum += n * amp;
    norm += amp;
    x *= lacunarity; y *= lacunarity;
    amp *= gain;
  }
  return sum / norm;
}

float fbm3(float x, float y, float z, uint32_t seed, int octaves) {
  float sum = 0, amp = 1, norm = 0;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * gradient3(x, y, z, seed + static_cast<uint32_t>(i) * 131u);
    norm += amp;
    x *= 2.0f; y *= 2.0f; z *= 2.0f;
    amp *= 0.5f;
  }
  return sum / norm;
}

float cellular2(float x, float y, uint32_t seed) {
  const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
  float best = 1e9f;
  for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
      const uint32_t h = hash2i(ix + ox, iy + oy, seed);
      const float px = static_cast<float>(ix + ox) + hashToFloat(h);
      const float py = static_cast<float>(iy + oy) + hashToFloat(hash32(h));
      const float dx = px - x, dy = py - y;
      best = std::fmin(best, dx * dx + dy * dy);
    }
  return std::sqrt(best);
}

}  // namespace aaa::noise
