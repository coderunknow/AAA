#include "world/world_layout.h"

#include <cmath>

#include "core/noise.h"
#include "core/rng.h"

namespace aaa {

float Polyline::distance(Vec2 p, float* outT) const {
  float best = 1e30f, bestLen = 0.0f;
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    const Vec2 a = points[i], b = points[i + 1];
    const Vec2 ab = b - a;
    const float len2 = dot(ab, ab);
    const float u = len2 > 0 ? clampf(dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    const Vec2 q = a + ab * u;
    const float d2 = dot(p - q, p - q);
    if (d2 < best) {
      best = d2;
      bestLen = cumLen[i] + (cumLen[i + 1] - cumLen[i]) * u;
    }
  }
  if (outT) *outT = totalLength() > 0 ? bestLen / totalLength() : 0.0f;
  return std::sqrt(best);
}

Vec2 Polyline::pointAt(float t) const {
  if (points.empty()) return {};
  const float target = clampf(t, 0.0f, 1.0f) * totalLength();
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    if (cumLen[i + 1] >= target) {
      const float seg = cumLen[i + 1] - cumLen[i];
      const float u = seg > 0 ? (target - cumLen[i]) / seg : 0.0f;
      return points[i] + (points[i + 1] - points[i]) * u;
    }
  }
  return points.back();
}

Polyline makeSpline(const std::vector<Vec2>& c, float spacing) {
  Polyline out;
  if (c.size() < 2) return out;
  auto cr = [](Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
            (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
  };
  std::vector<Vec2> dense;
  for (size_t i = 0; i + 1 < c.size(); ++i) {
    const Vec2 p0 = c[i == 0 ? 0 : i - 1], p1 = c[i], p2 = c[i + 1], p3 = c[i + 2 < c.size() ? i + 2 : i + 1];
    const int steps = 32;
    for (int s = 0; s < steps; ++s) dense.push_back(cr(p0, p1, p2, p3, static_cast<float>(s) / steps));
  }
  dense.push_back(c.back());
  // Resample to approximately uniform spacing.
  out.points.push_back(dense[0]);
  out.cumLen.push_back(0.0f);
  float acc = 0.0f;
  for (size_t i = 1; i < dense.size(); ++i) {
    acc += length(dense[i] - dense[i - 1]);
    if (acc >= spacing || i + 1 == dense.size()) {
      out.cumLen.push_back(out.cumLen.back() + length(dense[i] - out.points.back()));
      out.points.push_back(dense[i]);
      acc = 0.0f;
    }
  }
  return out;
}

WorldLayout makeWorldLayout(uint32_t seed) {
  WorldLayout L;
  L.seed = seed;
  Rng rng(seed ^ 0x51ed270bu);
  auto jitter = [&](Vec2 p, float r) { return Vec2{p.x + rng.range(-r, r), p.y + rng.range(-r, r)}; };

  // Valley runs from the south-west mouth (low) to the north-east head (high).
  const std::vector<Vec2> valleyCtrl = {
      {-520, -470}, jitter({-380, -330}, 12), jitter({-250, -250}, 12), jitter({-140, -150}, 12),
      jitter({-60, -50}, 10), jitter({30, 0}, 10), jitter({120, 90}, 12), jitter({220, 190}, 12),
      jitter({320, 300}, 12), {470, 470}};
  L.valley = makeSpline(valleyCtrl, 4.0f);

  // Stream: meanders across the valley floor with a lateral offset.
  std::vector<Vec2> streamCtrl;
  for (int i = 0; i <= 40; ++i) {
    const float t = static_cast<float>(i) / 40.0f;
    const Vec2 p = L.valley.pointAt(t);
    const Vec2 q = L.valley.pointAt(clampf(t + 0.01f, 0.0f, 1.0f));
    const Vec2 p0 = L.valley.pointAt(clampf(t - 0.01f, 0.0f, 1.0f));
    Vec2 dir = q - p0;
    const float dl = length(dir);
    dir = dl > 0 ? dir * (1.0f / dl) : Vec2{1, 0};
    const Vec2 side{-dir.y, dir.x};
    const float meander = 16.0f * std::sin(t * 23.0f + 1.3f) + 7.0f * noise::gradient2(t * 9.0f, 0.5f, seed);
    streamCtrl.push_back(p + side * meander);
  }
  L.stream = makeSpline(streamCtrl, 2.0f);

  // Spawn: a small mossy clearing on the stream's bank, low in the valley.
  {
    float t = 0.30f;
    const Vec2 s = L.stream.pointAt(t);
    const Vec2 v = L.valley.pointAt(t);
    Vec2 away = s - v;
    float l = length(away);
    away = l > 0.5f ? away * (1.0f / l) : Vec2{0.7f, -0.7f};
    L.spawnClearing = {s + away * 18.0f, 16.0f};
    L.spawn = s + away * 12.0f;
  }

  // Shrine terrace on a spur above the valley, east of the centre.
  {
    const Vec2 v = L.valley.pointAt(0.62f);
    L.shrine = v + Vec2{70.0f, -55.0f};
  }

  // Trail: spawn -> along the valley -> switchbacks up to the shrine.
  {
    const Vec2 a = L.spawn;
    const Vec2 v1 = L.valley.pointAt(0.42f) + Vec2{14, -10};
    const Vec2 v2 = L.valley.pointAt(0.55f) + Vec2{22, -18};
    const Vec2 sw1 = L.shrine + Vec2{-38, 8};
    const Vec2 sw2 = L.shrine + Vec2{-14, -26};
    L.trail = makeSpline({a, v1, v2, sw1, sw2, L.shrine}, 2.0f);
  }

  L.clearings = {L.spawnClearing, {L.valley.pointAt(0.48f) + Vec2{-30, 25}, 14.0f},
                 {L.valley.pointAt(0.78f) + Vec2{20, -20}, 18.0f}};
  L.bambooGroves = {{L.valley.pointAt(0.22f) + Vec2{-35, 30}, 38.0f},
                    {L.valley.pointAt(0.52f) + Vec2{-45, 40}, 32.0f},
                    {L.valley.pointAt(0.70f) + Vec2{30, -50}, 26.0f}};
  // Karst limestone spires: north-west flank of the valley and around the head.
  L.karstFields = {{L.valley.pointAt(0.45f) + Vec2{-120, 110}, 70.0f},
                   {L.valley.pointAt(0.80f) + Vec2{-90, 90}, 60.0f},
                   {L.shrine + Vec2{60, -40}, 35.0f}};
  return L;
}

}  // namespace aaa
