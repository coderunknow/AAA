#include "core/math.h"
#include "core/noise.h"
#include "core/rng.h"
#include "test.h"

using namespace aaa;

TEST_CASE("math: rotationY turns forward toward +X") {
  const Vec3 f = Mat4::rotationY(kPi * 0.5f).transformDir({0, 0, 1});
  CHECK_NEAR(f.x, 1.0, 1e-5);
  CHECK_NEAR(f.z, 0.0, 1e-5);
  const Vec3 g = Mat4::rotationY(0.3f).transformDir({0, 0, 1});
  CHECK_NEAR(g.x, std::sin(0.3f), 1e-5);
  CHECK_NEAR(g.z, std::cos(0.3f), 1e-5);
}

TEST_CASE("math: matrix composition order (A*B applies B first)") {
  const Mat4 m = Mat4::translation({10, 0, 0}) * Mat4::scale({2, 2, 2});
  const Vec3 p = m.transformPoint({1, 1, 1});
  CHECK_NEAR(p.x, 12.0, 1e-5);
  CHECK_NEAR(p.y, 2.0, 1e-5);
}

TEST_CASE("math: damp factor is frame-rate independent") {
  float a = 0.0f, b = 0.0f;
  for (int i = 0; i < 60; ++i) a = lerp(a, 1.0f, dampFactor(0.2f, 1.0f / 60.0f));
  for (int i = 0; i < 30; ++i) b = lerp(b, 1.0f, dampFactor(0.2f, 1.0f / 30.0f));
  CHECK_NEAR(a, b, 1e-4);
}

TEST_CASE("noise: deterministic and bounded") {
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < 20000; ++i) {
    const float x = i * 0.137f, y = i * 0.071f;
    const float n = noise::gradient2(x, y, 7);
    CHECK(n == noise::gradient2(x, y, 7));
    lo = std::fmin(lo, n); hi = std::fmax(hi, n);
  }
  CHECK(lo >= -1.5f && hi <= 1.5f);
  CHECK(hi - lo > 1.0f);  // not degenerate
  const float r = noise::ridged2(3.3f, 4.4f, 1, 5);
  CHECK(r >= 0.0f && r <= 1.0f);
}

TEST_CASE("rng: reproducible sequences") {
  Rng a(42), b(42), c(43);
  bool differs = false;
  for (int i = 0; i < 100; ++i) {
    const uint32_t x = a.next();
    CHECK(x == b.next());
    differs |= x != c.next();
  }
  CHECK(differs);
}
