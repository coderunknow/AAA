#pragma once
// Small value-type math library for gameplay/world code.
// Conventions: Y up, left-handed (x right, z forward) — matches bx/bgfx defaults.
// Mat4 is column-major (m[col*4 + row]) with translation in m[12..14], i.e. the
// same memory layout bgfx expects for transforms; Mat4 * Vec transforms a point.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aaa {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float e0, float e1, float x) {
  const float t = saturate((x - e0) / (e1 - e0));
  return t * t * (3.0f - 2.0f * t);
}
inline float radians(float deg) { return deg * (kPi / 180.0f); }
// Frame-rate independent exponential smoothing factor for a given half-life (seconds).
inline float dampFactor(float halfLife, float dt) {
  return halfLife <= 0.0f ? 1.0f : 1.0f - std::exp2(-dt / halfLife);
}
inline float wrapAngle(float a) {
  while (a > kPi) a -= kTwoPi;
  while (a < -kPi) a += kTwoPi;
  return a;
}

struct Vec2 {
  float x = 0, y = 0;
  constexpr Vec2() = default;
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
};
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

struct Vec3 {
  float x = 0, y = 0, z = 0;
  constexpr Vec3() = default;
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator*(Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  Vec2 xz() const { return {x, z}; }
};
inline Vec3 operator*(float s, Vec3 v) { return v * s; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline float lengthSq(Vec3 a) { return dot(a, a); }
inline Vec3 normalize(Vec3 a) {
  const float l = length(a);
  return l > 1e-8f ? a / l : Vec3{0, 0, 0};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
  constexpr Vec4() = default;
  constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  constexpr Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
};

struct Mat4 {
  float m[16];

  static Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }
  static Mat4 translation(Vec3 t) {
    Mat4 r = identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
  }
  static Mat4 scale(Vec3 s) {
    Mat4 r = identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
  }
  // Rotation about an arbitrary axis. rotationY(yaw) maps +Z (forward) to
  // (sin yaw, 0, cos yaw), i.e. positive yaw turns forward toward +X (right).
  static Mat4 rotation(Vec3 axis, float angle) {
    const Vec3 a = normalize(axis);
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    Mat4 r = identity();
    r.m[0] = t * a.x * a.x + c;       r.m[4] = t * a.x * a.y - s * a.z;  r.m[8]  = t * a.x * a.z + s * a.y;
    r.m[1] = t * a.x * a.y + s * a.z; r.m[5] = t * a.y * a.y + c;        r.m[9]  = t * a.y * a.z - s * a.x;
    r.m[2] = t * a.x * a.z - s * a.y; r.m[6] = t * a.y * a.z + s * a.x;  r.m[10] = t * a.z * a.z + c;
    return r;
  }
  static Mat4 rotationY(float angle) { return rotation({0, 1, 0}, angle); }
  static Mat4 rotationX(float angle) { return rotation({1, 0, 0}, angle); }
  static Mat4 rotationZ(float angle) { return rotation({0, 0, 1}, angle); }

  Mat4 operator*(const Mat4& b) const {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
      for (int rr = 0; rr < 4; ++rr) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * b.m[c * 4 + k];
        r.m[c * 4 + rr] = s;
      }
    return r;
  }
  Vec3 transformPoint(Vec3 p) const {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
  }
  Vec3 transformDir(Vec3 d) const {
    return {m[0] * d.x + m[4] * d.y + m[8] * d.z,
            m[1] * d.x + m[5] * d.y + m[9] * d.z,
            m[2] * d.x + m[6] * d.y + m[10] * d.z};
  }
  Vec3 position() const { return {m[12], m[13], m[14]}; }
};

}  // namespace aaa
