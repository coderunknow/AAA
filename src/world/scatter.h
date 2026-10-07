#pragma once
// Deterministic rule-based placement of vegetation and props.
#include <array>
#include <cstdint>
#include <vector>

#include "core/math.h"

namespace aaa {

class Heightfield;
struct WorldFields;
struct WorldLayout;

enum class PropKind : uint8_t {
  Pine,        // mature mountain pine, layered flat canopy
  PineYoung,   // younger, conical pine at forest edges
  Bamboo,      // bamboo clump
  Fern,        // understory fern
  Rock,        // small/medium mossy rock
  Boulder,     // large boulder
  KarstSpire,  // limestone spire / pillar
  FallenLog,   // decaying log
  Grass,       // ground-cover clump (rendered only near the camera)
  Count
};
constexpr int kPropKindCount = static_cast<int>(PropKind::Count);
const char* propKindName(PropKind k);

struct PropInstance {
  Vec3 position;
  float scale = 1.0f;
  float yaw = 0.0f;
  float tint = 0.5f;      // per-instance colour variation [0,1]
  uint16_t variant = 0;   // mesh variant index
  PropKind kind = PropKind::Rock;
};

struct CircleCollider {
  Vec2 center;
  float radius;
  float top;  // world-space height of the collider top (for camera / jump checks)
};

struct ScatterResult {
  std::array<std::vector<PropInstance>, kPropKindCount> instances;
  std::vector<CircleCollider> colliders;
};

struct ScatterContext {
  const WorldLayout* layout;
  const Heightfield* heightfield;
  const WorldFields* fields;
};

// Scatter one square region [x0, x0+size) x [z0, z0+size). Results depend only on
// (seed, region), never on generation order.
constexpr uint32_t kAllKindsExceptGrass = ((1u << kPropKindCount) - 1u) & ~(1u << static_cast<int>(PropKind::Grass));
constexpr uint32_t kGrassOnly = 1u << static_cast<int>(PropKind::Grass);
void scatterRegion(const ScatterContext& ctx, float x0, float z0, float size, ScatterResult& out,
                   uint32_t kindMask = kAllKindsExceptGrass);

// Density functions exposed for tests and debug views (probability per grid cell).
float propDensity(const ScatterContext& ctx, PropKind kind, float x, float z);

}  // namespace aaa
