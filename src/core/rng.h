#pragma once
// Deterministic hashing and random numbers. Identical results on native and WASM:
// only integer arithmetic and IEEE float conversions are used.
#include <cstdint>

namespace aaa {

inline uint32_t hash32(uint32_t x) {
  // PCG-style integer hash (O'Neill / Jarzynski & Olano "Hash Functions for GPU Rendering").
  uint32_t state = x * 747796405u + 2891336453u;
  uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
inline uint32_t hashCombine(uint32_t a, uint32_t b) { return hash32(a ^ (hash32(b) + 0x9e3779b9u + (a << 6) + (a >> 2))); }
inline uint32_t hash2i(int32_t x, int32_t y, uint32_t seed) {
  return hashCombine(hashCombine(seed, static_cast<uint32_t>(x)), static_cast<uint32_t>(y));
}
// [0,1)
inline float hashToFloat(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

class Rng {
 public:
  explicit Rng(uint32_t seed) : state_(hash32(seed) | 1u) {}
  uint32_t next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }
  float nextFloat() { return hashToFloat(next()); }                       // [0,1)
  float range(float lo, float hi) { return lo + (hi - lo) * nextFloat(); }
  int rangeInt(int lo, int hiInclusive) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hiInclusive - lo + 1)); }

 private:
  uint32_t state_;
};

}  // namespace aaa
