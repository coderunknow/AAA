#include "render/water_renderer.h"

#include <cmath>

#include "render/gpu_mesh.h"
#include "render/shader_library.h"
#include "world/world.h"

namespace aaa {
namespace {
struct WaterVertex { float x, y, z, u, v; };
constexpr float kHalfWidth = 9.0f;      // extends under the banks; terrain depth clips the visible edge
constexpr float kSegmentLength = 48.0f;  // culling granularity
}  // namespace

WaterRenderer::~WaterRenderer() {
  if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
  if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
}

void WaterRenderer::init(const World& world, ShaderLibrary& shaders, bgfx::UniformHandle detailSampler,
                         bgfx::TextureHandle detail) {
  sDetail_ = detailSampler;
  detail_ = detail;
  program_ = shaders.program("vs_water", "fs_water");
  const Polyline& line = world.layout().stream;
  const size_t n = line.points.size();
  if (n < 2) return;
  const float half = world.heightfield().halfSize() - 2.0f;
  std::vector<WaterVertex> verts;
  std::vector<uint32_t> idx;
  verts.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    const Vec2 p = line.points[i];
    const Vec2 a = line.points[i > 0 ? i - 1 : i], b = line.points[i + 1 < n ? i + 1 : i];
    Vec2 t = b - a;
    const float tl = length(t);
    t = tl > 1e-4f ? t * (1.0f / tl) : Vec2{1.0f, 0.0f};
    const Vec2 side{-t.y, t.x};
    const float y = world.waterSurface(clampf(p.x, -half, half), clampf(p.y, -half, half));
    const float along = line.cumLen[i];
    for (int s = -1; s <= 1; s += 2) {
      const Vec2 q = p + side * (kHalfWidth * static_cast<float>(s));
      verts.push_back({q.x, y, q.y, static_cast<float>(s), along});
    }
  }
  // Triangles facing +Y (cross(b-a, c-a) up) and segment bookkeeping for culling.
  size_t segStart = 0;
  uint32_t segFirst = 0;
  for (size_t i = 0; i + 1 < n; ++i) {
    const uint32_t l0 = static_cast<uint32_t>(i * 2), r0 = l0 + 1, l1 = l0 + 2, r1 = l0 + 3;
    // Determine winding numerically so either polyline direction works.
    const Vec3 A{verts[l0].x, verts[l0].y, verts[l0].z}, B{verts[l1].x, verts[l1].y, verts[l1].z},
        C{verts[r0].x, verts[r0].y, verts[r0].z};
    if (cross(B - A, C - A).y > 0.0f) idx.insert(idx.end(), {l0, l1, r0, r0, l1, r1});
    else idx.insert(idx.end(), {l0, r0, l1, r0, r1, l1});
    const bool last = i + 2 == n;
    if (line.cumLen[i + 1] - line.cumLen[segStart] >= kSegmentLength || last) {
      Vec3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
      for (size_t k = segStart * 2; k <= (i + 1) * 2 + 1; ++k) {
        const Vec3 v{verts[k].x, verts[k].y, verts[k].z};
        mn = {std::fmin(mn.x, v.x), std::fmin(mn.y, v.y), std::fmin(mn.z, v.z)};
        mx = {std::fmax(mx.x, v.x), std::fmax(mx.y, v.y), std::fmax(mx.z, v.z)};
      }
      const uint32_t end = static_cast<uint32_t>(idx.size());
      segments_.push_back({segFirst, end - segFirst, (mn + mx) * 0.5f, length(mx - mn) * 0.5f + 1.0f});
      segFirst = end;
      segStart = i + 1;
    }
  }
  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
  vbh_ = bgfx::createVertexBuffer(bgfx::copy(verts.data(), static_cast<uint32_t>(verts.size() * sizeof(WaterVertex))), layout);
  ibh_ = bgfx::createIndexBuffer(bgfx::copy(idx.data(), static_cast<uint32_t>(idx.size() * 4)), BGFX_BUFFER_INDEX32);
  gpuMemoryStats().meshBytes += verts.size() * sizeof(WaterVertex) + idx.size() * 4;
}

void WaterRenderer::submit(bgfx::ViewId view, const Frustum& frustum) {
  drawn_ = 0;
  if (!bgfx::isValid(program_) || !bgfx::isValid(vbh_)) return;
  for (const Segment& s : segments_) {
    if (!frustum.sphereVisible(s.center, s.radius)) continue;
    bgfx::setVertexBuffer(0, vbh_);
    bgfx::setIndexBuffer(ibh_, s.firstIndex, s.numIndices);
    bgfx::setTexture(3, sDetail_, detail_);
    bindSceneShadowMap();
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_CULL_CCW);
    bgfx::submit(view, program_);
    ++drawn_;
  }
}

}  // namespace aaa
