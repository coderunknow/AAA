#pragma once
// Procedural vegetation. Each generator is deterministic for a given seed and LOD.
// LOD 0 = close-up, LOD 1 = mid distance, LOD 2 = far (very few cards).
#include <cstdint>

#include "procgen/mesh_builder.h"

namespace aaa::procgen {

// Foliage atlas tiles (see textures.h): 2x2 grid.
enum class FoliageTile : uint8_t { PineNeedles = 0, BambooLeaves = 1, FernFrond = 2, GrassBlades = 3 };

PropMesh makeMountainPine(uint32_t seed, int lod);  // ~14-18 m, layered flat crown (unit scale)
PropMesh makeYoungPine(uint32_t seed, int lod);     // ~6-8 m, conical
PropMesh makeBambooClump(uint32_t seed, int lod);   // clump of culms, ~8-11 m
PropMesh makeFern(uint32_t seed, int lod);          // ~0.9 m radius
PropMesh makeGrassClump(uint32_t seed, int lod);    // ~0.5 m tall
PropMesh makeFallenLog(uint32_t seed, int lod);     // ~5 m long

}  // namespace aaa::procgen
