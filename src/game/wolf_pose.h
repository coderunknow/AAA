#pragma once
// Procedural pose for a wolf (rigid primitives, same pipeline as the survivor).
#include <array>

#include "game/character_animator.h"
#include "game/wildlife.h"

namespace aaa {

constexpr int kWolfPartCount = 20;
using WolfPose = std::array<PartPose, kWolfPartCount>;

void buildWolfPose(const Wolf& wolf, float time, WolfPose& out);

// Unit capsule (origin -> -Y length 1, radius 1) stretched between local points a and b.
Mat4 segmentTransform(const Mat4& root, Vec3 a, Vec3 b, float radius);

}  // namespace aaa
