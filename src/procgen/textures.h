#pragma once
// CPU-generated textures (RGBA8). Generated at load time from code so the asset
// pipeline stays reproducible without binary blobs in the repository.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aaa::procgen {

struct ImageRGBA8 {
  int width = 0, height = 0;
  std::vector<uint8_t> pixels;  // width * height * 4
  uint8_t* at(int x, int y) { return &pixels[(static_cast<size_t>(y) * width + x) * 4]; }
};

// Mip chain with alpha-coverage preservation for alpha-tested foliage.
std::vector<ImageRGBA8> buildMipChain(const ImageRGBA8& base, bool preserveAlphaCoverage, float alphaRef = 0.5f);

// 2x2 atlas: pine needles, bamboo leaves, fern frond, grass blades. RGB = albedo
// (linear-ish, tinted per instance in the shader), A = coverage.
ImageRGBA8 makeFoliageAtlas(int size, uint32_t seed);

// Tileable detail noise: R = fbm, G = cellular cracks, B = fine grain, A = large blotches.
ImageRGBA8 makeDetailNoise(int size, uint32_t seed);

}  // namespace aaa::procgen
