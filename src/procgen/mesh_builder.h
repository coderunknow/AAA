#pragma once
// CPU-side mesh representation shared by all procedural generators.
// Vertex attributes:
//   position, normal, uv
//   color: r = baked ambient occlusion, g = wind weight (0 rigid .. 1 tip),
//          b = material flag (0 = bark/rock, 1 = foliage card), a = per-branch phase
#include <cstdint>
#include <vector>

#include "core/math.h"

namespace aaa::procgen {

// Winding convention: a triangle (a,b,c) faces the direction of cross(b-a, c-a).
// With bx's left-handed projection that is clockwise on screen (cull CCW).
struct Vertex {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
  uint8_t r, g, b, a;
};
static_assert(sizeof(Vertex) == 36, "vertex layout must stay tightly packed");

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  Vec3 boundsMin{1e30f, 1e30f, 1e30f};
  Vec3 boundsMax{-1e30f, -1e30f, -1e30f};

  uint32_t addVertex(Vec3 p, Vec3 n, Vec2 uv, float ao = 1.0f, float wind = 0.0f, float flag = 0.0f, float phase = 0.0f);
  void addTriangle(uint32_t a, uint32_t b, uint32_t c) { indices.insert(indices.end(), {a, b, c}); }
  void addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { addTriangle(a, b, c); addTriangle(a, c, d); }
  void append(const MeshData& other);
  void recomputeNormals();  // smooth normals from triangles
  void computeBounds();
  bool empty() const { return indices.empty(); }
  size_t triangleCount() const { return indices.size() / 3; }
};

// A renderable asset: opaque part (bark/rock) + alpha-tested part (foliage cards).
struct PropMesh {
  MeshData opaque;
  MeshData foliage;
  Vec3 boundsMin, boundsMax;
  void finalize();
};

inline uint8_t toUnorm8(float v) { return static_cast<uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// Tapered tube along a centreline. radii.size() == centre.size().
void addTube(MeshData& m, const std::vector<Vec3>& centre, const std::vector<float>& radii, int sides,
             float uvScaleV, float windBase, float windTip, float phase, bool capEnd);

}  // namespace aaa::procgen
