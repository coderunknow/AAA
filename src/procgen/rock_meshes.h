#pragma once
#include <cstdint>

#include "procgen/mesh_builder.h"

namespace aaa::procgen {

PropMesh makeRock(uint32_t seed, int lod);        // ~1 m mossy rock (unit scale)
PropMesh makeBoulder(uint32_t seed, int lod);     // ~3 m boulder with strata
PropMesh makeKarstSpire(uint32_t seed, int lod);  // ~30-45 m limestone pillar

}  // namespace aaa::procgen
