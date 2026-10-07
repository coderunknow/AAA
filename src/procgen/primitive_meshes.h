#pragma once
#include "procgen/mesh_builder.h"

namespace aaa::procgen {

// Unit primitives used by the stand-in character (see CharacterAnimator).
MeshData makeCapsule(int sides = 12, int capRings = 4);  // radius 1, hangs from origin to y = -1
MeshData makeBox();                                      // [-1,1]^3 with bevelled normals
MeshData makeSphere(int sides = 16, int rings = 10);     // radius 1
MeshData makeCone(int sides = 20);                       // brim radius 1 at y = -0.3, apex y = 1 (hat)

}  // namespace aaa::procgen
