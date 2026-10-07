#include "game/wolf_pose.h"

#include <cmath>

namespace aaa {

Mat4 segmentTransform(const Mat4& root, Vec3 a, Vec3 b, float radius) {
  Vec3 y = a - b;  // the primitive's +Y points from b back to a
  const float len = std::fmax(length(y), 1e-4f);
  y = y / len;
  const Vec3 ref = std::fabs(y.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 x = normalize(cross(ref, y));
  const Vec3 z = cross(x, y);
  Mat4 m = Mat4::identity();
  m.m[0] = x.x * radius; m.m[1] = x.y * radius; m.m[2] = x.z * radius;
  m.m[4] = y.x * len;    m.m[5] = y.y * len;    m.m[6] = y.z * len;
  m.m[8] = z.x * radius; m.m[9] = z.y * radius; m.m[10] = z.z * radius;
  m.m[12] = a.x; m.m[13] = a.y; m.m[14] = a.z;
  return root * m;
}

namespace {
Mat4 ellipsoid(const Mat4& root, Vec3 c, Vec3 r) { return root * Mat4::translation(c) * Mat4::scale(r); }
}  // namespace

void buildWolfPose(const Wolf& w, float time, WolfPose& out) {
  const float speed = w.speed;
  const float gait = smoothstep(0.05f, 1.2f, speed);       // 0 standing .. 1 moving
  const float stride = 0.35f + 0.25f * smoothstep(3.0f, 8.0f, speed);
  const float ph = w.gaitPhase * kTwoPi;
  const float breathe = std::sin(time * 2.2f + static_cast<float>(w.rng % 7)) * 0.006f;
  const float bob = std::fabs(std::sin(ph)) * 0.035f * gait;
  // Stalking wolves keep their heads low; charging ones stretch out.
  const float headLow = w.state == WolfState::Stalk ? 0.12f : (w.state == WolfState::Charge ? 0.06f : 0.0f);

  const Mat4 root = Mat4::translation(w.pos + Vec3{0, bob + breathe, 0}) * Mat4::rotationY(w.yaw);
  int i = 0;
  auto put = [&](const Mat4& m, PartShape s, PartMaterial mat) { out[i++] = PartPose{m, s, mat}; };

  // Torso, chest and haunch masses.
  put(segmentTransform(root, {0, 0.63f, 0.30f}, {0, 0.61f, -0.36f}, 0.165f), PartShape::Capsule, PartMaterial::Fur);
  put(ellipsoid(root, {0, 0.61f, 0.26f}, {0.17f, 0.21f, 0.24f}), PartShape::Sphere, PartMaterial::Fur);
  put(ellipsoid(root, {0, 0.63f, -0.32f}, {0.15f, 0.17f, 0.19f}), PartShape::Sphere, PartMaterial::Fur);
  put(ellipsoid(root, {0, 0.73f, 0.12f}, {0.11f, 0.08f, 0.26f}), PartShape::Sphere, PartMaterial::FurDark);  // saddle

  // Neck and head.
  const Vec3 headC{0, 0.80f - headLow, 0.62f - headLow * 0.3f};
  put(segmentTransform(root, headC + Vec3{0, -0.03f, -0.06f}, {0, 0.66f, 0.36f}, 0.11f), PartShape::Capsule, PartMaterial::Fur);
  put(ellipsoid(root, headC, {0.11f, 0.105f, 0.13f}), PartShape::Sphere, PartMaterial::Fur);
  put(segmentTransform(root, headC + Vec3{0, -0.035f, 0.24f}, headC + Vec3{0, -0.01f, 0.05f}, 0.05f), PartShape::Capsule,
      PartMaterial::FurDark);
  for (float s : {-1.0f, 1.0f}) {
    put(ellipsoid(root, headC + Vec3{0.055f * s, 0.10f, -0.02f}, {0.034f, 0.065f, 0.022f}), PartShape::Cone,
        PartMaterial::FurDark);
    put(ellipsoid(root, headC + Vec3{0.048f * s, 0.035f, 0.11f}, {0.013f, 0.011f, 0.011f}), PartShape::Sphere,
        PartMaterial::Eyes);
  }

  // Legs: trot (diagonal pairs move together). Front legs bend back at the wrist, hind at the hock.
  struct Leg { float x, y, z, offset; bool front; };
  const Leg legs[4] = {{-0.10f, 0.58f, 0.30f, 0.0f, true}, {0.10f, 0.58f, 0.30f, 0.5f, true},
                       {-0.10f, 0.57f, -0.34f, 0.5f, false}, {0.10f, 0.57f, -0.34f, 0.0f, false}};
  for (const Leg& L : legs) {
    const float p = ph + L.offset * kTwoPi;
    const float swing = std::sin(p) * stride * gait;
    const float lift = std::fmax(0.0f, std::cos(p)) * 0.9f * gait;
    const Vec3 hip{L.x, L.y, L.z};
    const float a = swing + (L.front ? 0.0f : 0.15f);
    const Vec3 knee = hip + Vec3{0, -std::cos(a), std::sin(a)} * 0.29f;
    const float b = L.front ? a - lift * 0.9f : a - 0.45f + lift * 0.5f;
    const Vec3 foot = knee + Vec3{0, -std::cos(b), std::sin(b)} * 0.30f;
    put(segmentTransform(root, hip, knee, 0.05f), PartShape::Capsule, PartMaterial::Fur);
    put(segmentTransform(root, knee, foot, 0.034f), PartShape::Capsule, PartMaterial::FurDark);
  }

  // Tail: hangs low while stalking, wags gently otherwise.
  const float wag = std::sin(time * (w.state == WolfState::Roam ? 3.0f : 1.2f)) * 0.08f;
  const float tailDrop = w.state == WolfState::Stalk ? 0.1f : 0.0f;
  put(segmentTransform(root, {0, 0.64f, -0.46f}, {wag, 0.38f - tailDrop, -0.72f}, 0.055f), PartShape::Capsule,
      PartMaterial::Fur);
  while (i < kWolfPartCount) out[i++] = PartPose{Mat4::scale({0, 0, 0}), PartShape::Sphere, PartMaterial::Fur};
}

}  // namespace aaa
