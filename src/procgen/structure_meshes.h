#pragma once
// Procedural meshes for hand-placed structures and interactables: the shrine, forage spots and
// campfires. Each piece is split by material so one shader with per-draw material constants
// can render it (see ObjectRenderer).
#include <vector>

#include "procgen/mesh_builder.h"

namespace aaa {
struct ShrineLayout;
class Heightfield;
}  // namespace aaa

namespace aaa::procgen {

enum class StructMaterial : uint8_t {
  Stone, MossStone, Wood, Lacquer, RoofTile, Deadwood, Flint, Leaf, Berry, MushCap, MushStem, Charcoal, Ember, Ash, Count
};
constexpr int kStructMaterialCount = static_cast<int>(StructMaterial::Count);

struct MaterialMesh {
  StructMaterial material;
  MeshData mesh;
};

// Transforms `src` by `xf` (normals renormalised, winding fixed for mirroring) and appends it.
void appendTransformed(MeshData& dst, const MeshData& src, const Mat4& xf);
// Unit cube [-1,1]^3 with flat faces through `xf`; uv in metres * uvScale.
void addBox(MeshData& m, const Mat4& xf, float uvScale = 1.0f);

// World-space shrine meshes, one per material (empty materials omitted).
std::vector<MaterialMesh> buildShrineMeshes(const ShrineLayout& shrine, const Heightfield& hf);

// Local-space meshes (origin on the ground). `part` 0 = always drawn; part 1 = only while
// the spot is available (e.g. berries on a bush).
struct PickupMeshPart {
  StructMaterial material;
  int part;
  MeshData mesh;
};
std::vector<PickupMeshPart> buildPickupMesh(int kind, int variant);

// Campfire: stones + logs (part 0), embers (part 1, emissive), ash bed (part 2, burnt out).
std::vector<PickupMeshPart> buildCampfireMesh();

}  // namespace aaa::procgen
