#pragma once
// Deterministic 2D/3D gradient noise and fractal helpers used by world generation
// and procedural asset generation.
#include <cstdint>

namespace aaa::noise {

float gradient2(float x, float y, uint32_t seed);              // ~[-1,1]
float gradient3(float x, float y, float z, uint32_t seed);     // ~[-1,1]
float fbm2(float x, float y, uint32_t seed, int octaves, float lacunarity = 2.0f, float gain = 0.5f);
float ridged2(float x, float y, uint32_t seed, int octaves, float lacunarity = 2.0f, float gain = 0.5f);  // [0,1]
float fbm3(float x, float y, float z, uint32_t seed, int octaves);
// Worley/cellular F1 distance in cell units.
float cellular2(float x, float y, uint32_t seed);

}  // namespace aaa::noise
