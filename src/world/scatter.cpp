#include "world/scatter.h"

#include <cmath>

#include "core/noise.h"
#include "core/rng.h"
#include "world/heightfield.h"
#include "world/terrain_generator.h"
#include "world/world_layout.h"

namespace aaa {

const char* propKindName(PropKind k) {
  static const char* kNames[] = {"pine", "pine_young", "bamboo", "fern", "rock", "boulder", "karst_spire", "fallen_log", "grass"};
  return kNames[static_cast<int>(k)];
}

namespace {

struct KindRule {
  float cell;      // jittered-grid spacing (m)
  float minScale, maxScale;
};

constexpr KindRule kRules[kPropKindCount] = {
    {7.0f, 0.8f, 1.35f},   // Pine
    {9.0f, 0.5f, 1.0f},    // PineYoung
    {3.2f, 0.8f, 1.25f},   // Bamboo
    {2.4f, 0.6f, 1.3f},    // Fern
    {5.0f, 0.35f, 1.4f},   // Rock
    {19.0f, 0.8f, 1.8f},   // Boulder
    {24.0f, 0.7f, 1.5f},   // KarstSpire
    {21.0f, 0.8f, 1.2f},   // FallenLog
    {0.62f, 0.55f, 1.35f},  // Grass
};

struct Site {
  float h, slope, stream, trail, valley, forest, clearing, grove, karst, shrine, moist, alt;
};

float membership(const std::vector<Clearing>& list, Vec2 p, float inner, float outer) {
  float m = 0.0f;
  for (const Clearing& c : list) {
    const float d = length(p - c.center);
    m = std::fmax(m, 1.0f - smoothstep(c.radius * inner, c.radius * outer, d));
  }
  return m;
}

Site evaluate(const ScatterContext& ctx, float x, float z) {
  const WorldLayout& L = *ctx.layout;
  const Vec2 p{x, z};
  Site s{};
  s.h = ctx.heightfield->height(x, z);
  s.slope = ctx.heightfield->slope(x, z);
  s.stream = ctx.fields->stream(x, z);
  s.trail = ctx.fields->trail(x, z);
  s.valley = ctx.fields->valley(x, z);
  // Forest patchiness: large connected stands with natural gaps.
  s.forest = smoothstep(-0.3f, 0.35f, noise::fbm2(x / 85.0f, z / 85.0f, L.seed + 101, 4));
  s.clearing = membership(L.clearings, p, 0.55f, 1.15f);
  s.grove = membership(L.bambooGroves, p, 0.45f, 1.0f);
  s.karst = membership(L.karstFields, p, 0.3f, 1.0f);
  s.shrine = 1.0f - smoothstep(L.shrineTerraceRadius - 4.0f, L.shrineTerraceRadius + 6.0f, length(p - L.shrine));
  s.moist = 1.0f - smoothstep(4.0f, 55.0f, s.stream);
  s.alt = s.h - L.valleyFloorAt(ctx.fields->valleyParam(x, z));
  return s;
}

float densityAt(const Site& s, PropKind k) {
  const float trailClear = smoothstep(1.6f, 4.0f, s.trail);
  const float dry = smoothstep(3.0f, 6.5f, s.stream);       // not in the water
  const float treeLine = 1.0f - smoothstep(170.0f, 250.0f, s.alt);
  const float open = 1.0f - s.clearing;
  switch (k) {
    case PropKind::Pine: {
      const float slopeOk = 1.0f - smoothstep(0.38f, 0.6f, s.slope);
      const float base = 0.18f + 0.7f * s.forest;
      const float ridgeBias = 0.75f + 0.35f * smoothstep(10.0f, 60.0f, s.alt);
      return base * ridgeBias * slopeOk * dry * smoothstep(6.0f, 12.0f, s.stream) * open * (1.0f - 0.92f * s.grove) *
             trailClear * treeLine * (1.0f - s.shrine) * (1.0f - 0.6f * s.karst);
    }
    case PropKind::PineYoung: {
      const float edge = 0.25f + 0.6f * (1.0f - std::fabs(s.forest - 0.5f) * 2.0f);  // forest margins
      return 0.5f * edge * (1.0f - smoothstep(0.4f, 0.6f, s.slope)) * dry * (1.0f - 0.8f * s.clearing) *
             (1.0f - s.grove) * trailClear * treeLine * (1.0f - s.shrine);
    }
    case PropKind::Bamboo:
      return s.grove * 0.9f * smoothstep(4.0f, 7.0f, s.stream) * (1.0f - smoothstep(0.35f, 0.5f, s.slope)) *
             smoothstep(1.5f, 3.0f, s.trail) * open;
    case PropKind::Fern:
      return (0.12f + 0.5f * s.moist + 0.3f * s.forest) * (1.0f - smoothstep(0.4f, 0.55f, s.slope)) * dry *
             smoothstep(1.0f, 2.5f, s.trail) * (1.0f - 0.6f * s.clearing) * treeLine * (1.0f - s.shrine);
    case PropKind::Rock: {
      const float streamRocks = (1.0f - smoothstep(2.0f, 8.0f, s.stream)) * 0.55f;
      return std::fmin(1.0f, 0.06f + 0.7f * smoothstep(0.15f, 0.55f, s.slope) + streamRocks + 0.25f * s.karst) *
             smoothstep(0.8f, 2.0f, s.trail) * (1.0f - 0.7f * s.shrine);
    }
    case PropKind::Boulder:
      return (0.12f + 0.55f * smoothstep(0.2f, 0.6f, s.slope) + 0.4f * s.karst) * smoothstep(3.0f, 7.0f, s.stream) *
             smoothstep(3.0f, 6.0f, s.trail) * (1.0f - 0.8f * s.clearing) * (1.0f - s.shrine);
    case PropKind::KarstSpire:
      return (0.85f * s.karst + 0.04f * smoothstep(0.3f, 0.7f, s.slope) * smoothstep(40.0f, 90.0f, s.alt)) *
             smoothstep(8.0f, 14.0f, s.stream) * smoothstep(6.0f, 12.0f, s.trail) * (1.0f - s.clearing) *
             (1.0f - s.shrine);
    case PropKind::FallenLog:
      return 0.32f * s.forest * (1.0f - smoothstep(0.25f, 0.4f, s.slope)) * dry * trailClear * open *
             (1.0f - s.grove) * treeLine;
    case PropKind::Grass: {
      const float light = 0.55f + 0.45f * (1.0f - s.forest) + 0.4f * s.clearing;
      return std::fmin(1.0f, light) * (1.0f - smoothstep(0.35f, 0.55f, s.slope)) * smoothstep(3.0f, 4.5f, s.stream) *
             smoothstep(0.9f, 2.4f, s.trail) * (1.0f - 0.85f * s.grove) * treeLine *
             (1.0f - 0.7f * s.shrine);
    }
    case PropKind::Count: break;
  }
  return 0.0f;
}

}  // namespace

float propDensity(const ScatterContext& ctx, PropKind kind, float x, float z) {
  return densityAt(evaluate(ctx, x, z), kind);
}

void scatterRegion(const ScatterContext& ctx, float x0, float z0, float size, ScatterResult& out, uint32_t kindMask) {
  const uint32_t seed = ctx.layout->seed;
  for (int k = 0; k < kPropKindCount; ++k) {
    if (!(kindMask & (1u << k))) continue;
    const PropKind kind = static_cast<PropKind>(k);
    const KindRule& rule = kRules[k];
    // Grid aligned to world origin so neighbouring regions never double-place.
    const int gx0 = static_cast<int>(std::ceil(x0 / rule.cell)), gx1 = static_cast<int>(std::ceil((x0 + size) / rule.cell));
    const int gz0 = static_cast<int>(std::ceil(z0 / rule.cell)), gz1 = static_cast<int>(std::ceil((z0 + size) / rule.cell));
    for (int gz = gz0; gz < gz1; ++gz)
      for (int gx = gx0; gx < gx1; ++gx) {
        const uint32_t h = hash2i(gx, gz, seed * 31u + static_cast<uint32_t>(k) * 7919u);
        Rng rng(h);
        const float x = (gx + rng.range(-0.45f, 0.45f)) * rule.cell;
        const float z = (gz + rng.range(-0.45f, 0.45f)) * rule.cell;
        if (!ctx.heightfield->contains(x, z)) continue;
        const Site site = evaluate(ctx, x, z);
        const float density = densityAt(site, kind);
        if (rng.nextFloat() >= density) continue;

        PropInstance inst;
        inst.kind = kind;
        // Larger specimens where density is high (better growing conditions).
        inst.scale = lerp(rule.minScale, rule.maxScale, std::pow(rng.nextFloat(), 1.3f) * (0.6f + 0.4f * density));
        inst.yaw = rng.range(0.0f, kTwoPi);
        inst.tint = rng.nextFloat();
        inst.variant = static_cast<uint16_t>(rng.next() & 0xffffu);
        float sink = 0.0f;
        switch (kind) {
          case PropKind::Pine: case PropKind::PineYoung: sink = 0.25f; break;
          case PropKind::Rock: sink = 0.25f * inst.scale; break;
          case PropKind::Boulder: sink = 0.6f * inst.scale; break;
          case PropKind::KarstSpire: sink = 2.0f * inst.scale; break;
          case PropKind::FallenLog: sink = 0.12f; break;
          default: break;
        }
        inst.position = {x, site.h - sink, z};
        out.instances[k].push_back(inst);

        // Physical presence for the character controller and camera.
        const float s = inst.scale;
        switch (kind) {
          case PropKind::Pine: out.colliders.push_back({{x, z}, 0.34f * s, site.h + 18.0f * s}); break;
          case PropKind::PineYoung: out.colliders.push_back({{x, z}, 0.16f * s, site.h + 8.0f * s}); break;
          case PropKind::Bamboo: out.colliders.push_back({{x, z}, 0.45f * s, site.h + 10.0f * s}); break;
          case PropKind::Rock: if (s > 0.75f) out.colliders.push_back({{x, z}, 0.55f * s, site.h + 0.6f * s}); break;
          case PropKind::Boulder: out.colliders.push_back({{x, z}, 1.5f * s, site.h + 1.8f * s}); break;
          case PropKind::KarstSpire: out.colliders.push_back({{x, z}, 4.2f * s, site.h + 40.0f * s}); break;
          case PropKind::FallenLog: {
            // Log lies along its yaw; approximate with three circles.
            const Vec2 dir{std::sin(inst.yaw), std::cos(inst.yaw)};
            for (int i = -1; i <= 1; ++i)
              out.colliders.push_back({Vec2{x, z} + dir * (2.2f * s * i), 0.35f * s, site.h + 0.5f * s});
            break;
          }
          default: break;
        }
      }
  }
}

}  // namespace aaa
