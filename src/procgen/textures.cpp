#include "procgen/textures.h"

#include <cmath>

#include "core/math.h"
#include "core/noise.h"
#include "core/rng.h"
#include "procgen/mesh_builder.h"

namespace aaa::procgen {
namespace {

struct Canvas {
  int size;
  std::vector<float> rgba;  // premultiplied accumulation
  explicit Canvas(int s) : size(s), rgba(static_cast<size_t>(s) * s * 4, 0.0f) {}
  void plot(int x, int y, Vec3 c, float a) {
    if (x < 0 || y < 0 || x >= size || y >= size || a <= 0.0f) return;
    float* p = &rgba[(static_cast<size_t>(y) * size + x) * 4];
    // "over" compositing
    p[0] = c.x * a + p[0] * (1 - a);
    p[1] = c.y * a + p[1] * (1 - a);
    p[2] = c.z * a + p[2] * (1 - a);
    p[3] = a + p[3] * (1 - a);
  }
  // Anti-aliased thick line (capsule) from a to b.
  void line(Vec2 a, Vec2 b, float wa, float wb, Vec3 ca, Vec3 cb) {
    const int x0 = static_cast<int>(std::floor(std::fmin(a.x, b.x) - std::fmax(wa, wb) - 1));
    const int x1 = static_cast<int>(std::ceil(std::fmax(a.x, b.x) + std::fmax(wa, wb) + 1));
    const int y0 = static_cast<int>(std::floor(std::fmin(a.y, b.y) - std::fmax(wa, wb) - 1));
    const int y1 = static_cast<int>(std::ceil(std::fmax(a.y, b.y) + std::fmax(wa, wb) + 1));
    const Vec2 ab = b - a;
    const float len2 = std::fmax(dot(ab, ab), 1e-6f);
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const Vec2 p{x + 0.5f, y + 0.5f};
        const float t = clampf(dot(p - a, ab) / len2, 0.0f, 1.0f);
        const float w = lerp(wa, wb, t);
        const float d = length(p - (a + ab * t));
        const float cov = clampf(w - d + 0.5f, 0.0f, 1.0f);
        if (cov > 0) plot(x, y, lerp(ca, cb, t), cov);
      }
  }
  // Filled leaf shape (lanceolate) along a→b with max half-width w.
  void leaf(Vec2 a, Vec2 b, float w, Vec3 cBase, Vec3 cTip, uint32_t seed) {
    const Vec2 ab = b - a;
    const float len = length(ab);
    const Vec2 dir = ab * (1.0f / len);
    const Vec2 nrm{-dir.y, dir.x};
    const int steps = static_cast<int>(len * 1.5f) + 2;
    for (int i = 0; i <= steps; ++i) {
      const float t = static_cast<float>(i) / steps;
      const float hw = w * std::sin(std::pow(t, 0.7f) * kPi) * (1.0f - 0.2f * t);
      const Vec2 c = a + ab * t;
      const Vec3 col = lerp(cBase, cTip, t) * (0.9f + 0.1f * noise::gradient2(t * 8.0f, 0.0f, seed));
      line(c - nrm * hw, c + nrm * hw, 0.6f, 0.6f, col * 0.92f, col * 1.05f);
    }
    line(a, b, 0.5f, 0.3f, cBase * 0.75f, cTip * 0.8f);  // midrib
  }
};

void blit(ImageRGBA8& atlas, const Canvas& c, int ox, int oy) {
  for (int y = 0; y < c.size; ++y)
    for (int x = 0; x < c.size; ++x) {
      const float* p = &c.rgba[(static_cast<size_t>(y) * c.size + x) * 4];
      uint8_t* d = atlas.at(ox + x, oy + y);
      const float a = p[3];
      // Un-premultiply; fill transparent texels with a dilated average colour to avoid dark fringes.
      const Vec3 col = a > 1e-3f ? Vec3{p[0] / a, p[1] / a, p[2] / a} : Vec3{0.18f, 0.22f, 0.1f};
      d[0] = toUnorm8(col.x); d[1] = toUnorm8(col.y); d[2] = toUnorm8(col.z); d[3] = toUnorm8(a);
    }
}

}  // namespace


std::vector<ImageRGBA8> buildMipChain(const ImageRGBA8& base, bool preserveCoverage, float alphaRef) {
  std::vector<ImageRGBA8> chain{base};
  auto coverage = [&](const ImageRGBA8& img, float scale) {
    size_t n = 0;
    for (size_t i = 3; i < img.pixels.size(); i += 4) n += (img.pixels[i] / 255.0f) * scale > alphaRef ? 1 : 0;
    return static_cast<float>(n) / (img.pixels.size() / 4);
  };
  const float baseCov = coverage(base, 1.0f);
  while (chain.back().width > 1 && chain.back().height > 1) {
    const ImageRGBA8& src = chain.back();
    ImageRGBA8 dst;
    dst.width = src.width / 2;
    dst.height = src.height / 2;
    dst.pixels.resize(static_cast<size_t>(dst.width) * dst.height * 4);
    for (int y = 0; y < dst.height; ++y)
      for (int x = 0; x < dst.width; ++x) {
        float acc[4] = {0, 0, 0, 0}, wsum = 0;
        for (int k = 0; k < 4; ++k) {
          const uint8_t* s = &src.pixels[((static_cast<size_t>(y * 2 + k / 2)) * src.width + x * 2 + k % 2) * 4];
          const float a = s[3] / 255.0f;
          // Alpha-weighted colour average keeps edges from darkening.
          for (int c = 0; c < 3; ++c) acc[c] += s[c] * (a + 0.02f);
          wsum += a + 0.02f;
          acc[3] += s[3];
        }
        uint8_t* d = &dst.pixels[(static_cast<size_t>(y) * dst.width + x) * 4];
        for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(acc[c] / wsum + 0.5f);
        d[3] = static_cast<uint8_t>(acc[3] / 4.0f + 0.5f);
      }
    if (preserveCoverage && baseCov > 0.0f) {
      // Binary search an alpha scale so the alpha-tested coverage matches the base level
      // (Castaño, "Computing Alpha Mipmaps").
      float lo = 0.5f, hi = 4.0f;
      for (int it = 0; it < 10; ++it) {
        const float mid = 0.5f * (lo + hi);
        (coverage(dst, mid) < baseCov ? lo : hi) = mid;
      }
      const float s = 0.5f * (lo + hi);
      for (size_t i = 3; i < dst.pixels.size(); i += 4)
        dst.pixels[i] = static_cast<uint8_t>(clampf(dst.pixels[i] * s, 0.0f, 255.0f));
    }
    chain.push_back(std::move(dst));
  }
  return chain;
}

ImageRGBA8 makeFoliageAtlas(int size, uint32_t seed) {
  ImageRGBA8 atlas;
  atlas.width = atlas.height = size;
  atlas.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
  const int T = size / 2;
  const float s = static_cast<float>(T);

  {  // Tile 0: pine needle pad — twigs with dense radiating needle bundles (top view).
    Canvas c(T);
    Rng rng(seed + 1);
    const Vec2 centre{s * 0.5f, s * 0.5f};
    for (int twig = 0; twig < 9; ++twig) {
      const float a = twig * kTwoPi / 9 + rng.range(-0.2f, 0.2f);
      const Vec2 dir{std::cos(a), std::sin(a)};
      const Vec2 end = centre + dir * (s * rng.range(0.32f, 0.46f));
      c.line(centre, end, s * 0.008f, s * 0.004f, {0.2f, 0.14f, 0.09f}, {0.25f, 0.2f, 0.12f});
      for (int b = 0; b < 26; ++b) {
        const float t = rng.range(0.1f, 1.0f);
        const Vec2 p = centre + (end - centre) * t;
        for (int n = 0; n < 7; ++n) {
          const float na = a + rng.range(-1.2f, 1.2f);
          const float nl = s * rng.range(0.045f, 0.085f) * (1.1f - 0.4f * t);
          const Vec2 q = p + Vec2{std::cos(na), std::sin(na)} * nl;
          const float shade = rng.range(0.75f, 1.15f);
          c.line(p, q, s * 0.0045f, s * 0.002f, Vec3{0.11f, 0.2f, 0.09f} * shade, Vec3{0.2f, 0.33f, 0.14f} * shade);
        }
      }
    }
    blit(atlas, c, 0, 0);
  }
  {  // Tile 1: bamboo leaf spray — narrow lanceolate leaves on a thin twig.
    Canvas c(T);
    Rng rng(seed + 2);
    const Vec2 base{s * 0.08f, s * 0.5f};
    const Vec2 tip{s * 0.92f, s * 0.46f};
    c.line(base, tip, s * 0.006f, s * 0.003f, {0.36f, 0.4f, 0.2f}, {0.3f, 0.38f, 0.18f});
    for (int l = 0; l < 13; ++l) {
      const float t = rng.range(0.1f, 0.95f);
      const Vec2 p = base + (tip - base) * t;
      const float side = (l % 2) ? 1.0f : -1.0f;
      const float a = side * rng.range(0.35f, 0.9f) - 0.1f;
      const Vec2 dir{std::cos(a), std::sin(a)};
      const float len = s * rng.range(0.22f, 0.34f);
      const float shade = rng.range(0.8f, 1.1f);
      c.leaf(p, p + dir * len, s * 0.035f, Vec3{0.2f, 0.32f, 0.11f} * shade, Vec3{0.32f, 0.42f, 0.16f} * shade, seed + l);
    }
    blit(atlas, c, T, 0);
  }
  {  // Tile 2: fern frond — rachis with alternating pinnae, tip at the top (v = 0).
    Canvas c(T);
    Rng rng(seed + 3);
    const Vec2 base{s * 0.5f, s * 0.98f};
    const Vec2 tip{s * 0.5f, s * 0.03f};
    c.line(base, tip, s * 0.008f, s * 0.003f, {0.2f, 0.26f, 0.1f}, {0.24f, 0.34f, 0.12f});
    const int pinnae = 22;
    for (int i = 0; i < pinnae; ++i) {
      const float t = (i + 0.5f) / pinnae;
      const Vec2 p = base + (tip - base) * t;
      const float len = s * 0.42f * std::sin(std::fmin(1.0f, t * 1.15f + 0.05f) * kPi * 0.92f);
      for (int sd = -1; sd <= 1; sd += 2) {
        const Vec2 q = p + Vec2{sd * len, -len * 0.25f};
        const float shade = rng.range(0.85f, 1.1f);
        c.leaf(p, q, s * 0.022f, Vec3{0.15f, 0.27f, 0.08f} * shade, Vec3{0.25f, 0.38f, 0.12f} * shade, seed + i * 3 + sd);
      }
    }
    blit(atlas, c, 0, T);
  }
  {  // Tile 3: grass blades — tall thin blades, base at the bottom.
    Canvas c(T);
    Rng rng(seed + 4);
    for (int b = 0; b < 60; ++b) {
      const float x = rng.range(0.06f, 0.94f) * s;
      const float h = rng.range(0.45f, 0.97f) * s;
      const float bend = rng.range(-0.18f, 0.18f) * s;
      const Vec2 a{x, s * 0.995f};
      const Vec2 mid{x + bend * 0.4f, s - h * 0.55f};
      const Vec2 t{x + bend, s - h};
      const float shade = rng.range(0.75f, 1.2f);
      const Vec3 baseC = Vec3{0.16f, 0.22f, 0.07f} * shade;
      const Vec3 tipC = rng.nextFloat() < 0.25f ? Vec3{0.45f, 0.42f, 0.22f} * shade : Vec3{0.3f, 0.4f, 0.13f} * shade;
      c.line(a, mid, s * 0.012f, s * 0.008f, baseC, lerp(baseC, tipC, 0.5f));
      c.line(mid, t, s * 0.008f, s * 0.0015f, lerp(baseC, tipC, 0.5f), tipC);
    }
    blit(atlas, c, T, T);
  }
  return atlas;
}

ImageRGBA8 makeDetailNoise(int size, uint32_t seed) {
  ImageRGBA8 img;
  img.width = img.height = size;
  img.pixels.resize(static_cast<size_t>(size) * size * 4);
  // Tileable by sampling 3D noise on a torus.
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float u = static_cast<float>(x) / size * kTwoPi, v = static_cast<float>(y) / size * kTwoPi;
      auto torus = [&](float R, float r, float k, uint32_t s, int oct) {
        const float px = (R + r * std::cos(v)) * std::cos(u), py = (R + r * std::cos(v)) * std::sin(u), pz = r * std::sin(v);
        return noise::fbm3(px * k, py * k, pz * k, s, oct);
      };
      const float a = torus(1.0f, 0.5f, 2.2f, seed, 5);
      const float fine = torus(1.0f, 0.5f, 9.0f, seed + 7, 3);
      const float blot = torus(1.0f, 0.5f, 0.9f, seed + 13, 3);
      // Cellular cracks approximated by ridged fbm on the torus.
      const float cr = 1.0f - std::fabs(torus(1.0f, 0.5f, 4.0f, seed + 21, 3));
      uint8_t* p = img.at(x, y);
      p[0] = toUnorm8(a * 0.5f + 0.5f);
      p[1] = toUnorm8(std::pow(cr, 6.0f));
      p[2] = toUnorm8(fine * 0.5f + 0.5f);
      p[3] = toUnorm8(blot * 0.5f + 0.5f);
    }
  return img;
}

}  // namespace aaa::procgen
