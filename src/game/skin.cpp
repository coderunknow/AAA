#include "game/skin.h"

#include <algorithm>
#include <cmath>

namespace aaa {
namespace {

float pointSegmentDistanceSq(Vec3 p, Vec3 a, Vec3 b) {
  const Vec3 ab = b - a;
  const float denom = dot(ab, ab);
  const float t = denom > 1e-9f ? clampf(dot(p - a, ab) / denom, 0.0f, 1.0f) : 0.0f;
  const Vec3 c = a + ab * t;
  return lengthSq(p - c);
}

uint8_t toUnorm8(float v) { return static_cast<uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

}  // namespace

// ---------------------------------------------------------------------------
// Skeleton
// ---------------------------------------------------------------------------
int Skeleton::addBone(const char* name, int parent, Vec3 bindOffset) {
  Bone b;
  b.name = name;
  b.parent = parent;
  b.bindOffset = bindOffset;
  bones_.push_back(std::move(b));
  return static_cast<int>(bones_.size()) - 1;
}

int Skeleton::index(const char* name) const {
  for (size_t i = 0; i < bones_.size(); ++i)
    if (bones_[i].name == name) return static_cast<int>(i);
  return -1;
}

void Skeleton::finalize() {
  bindWorld_.assign(bones_.size(), Mat4::identity());
  inverseBind_.assign(bones_.size(), Mat4::identity());
  for (size_t i = 0; i < bones_.size(); ++i) {
    const Mat4 local = Mat4::translation(bones_[i].bindOffset);
    bindWorld_[i] = bones_[i].parent >= 0 ? bindWorld_[bones_[i].parent] * local : local;
    inverseBind_[i] = bindWorld_[i].inverseRigid();
  }
}

Vec3 Skeleton::bindTip(int i) const {
  const Bone& b = bones_[i];
  const Vec3 p = bindWorld_[i].position();
  // A bone points at its first child; leaves point up by a short stub.
  for (size_t c = 0; c < bones_.size(); ++c)
    if (bones_[c].parent == i) return bindWorld_[c].position();
  (void)b;
  return p + Vec3{0.0f, 0.12f, 0.0f};
}

// ---------------------------------------------------------------------------
// Pose
// ---------------------------------------------------------------------------
void Pose::resize(int joints) {
  world_.assign(static_cast<size_t>(std::max(joints, 0)), Mat4::identity());
  skin_.assign(world_.size(), Mat4::identity());
}

void Pose::computeSkin(const Skeleton& skeleton) {
  const int n = std::min(count(), skeleton.count());
  skin_.resize(static_cast<size_t>(std::max(n, 0)));
  for (int i = 0; i < n; ++i) {
    // world * inverseBind; both are rigid, so the product is rigid.
    const Mat4& w = world_[static_cast<size_t>(i)];
    const Mat4& ib = skeleton.inverseBind(i);
    Mat4 r;
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        float s = 0.0f;
        for (int k = 0; k < 4; ++k) s += w.m[k * 4 + row] * ib.m[c * 4 + k];
        r.m[c * 4 + row] = s;
      }
    skin_[static_cast<size_t>(i)] = r;
  }
}

void Pose::blend(const Pose& a, const Pose& b, float t, const Skeleton& skeleton) {
  const int n = std::min({a.count(), b.count(), count()});
  const float s = clampf(t, 0.0f, 1.0f);
  for (int i = 0; i < n; ++i) {
    const Mat4& x = a.world(i);
    const Mat4& y = b.world(i);
    Mat4 r;
    for (int k = 0; k < 16; ++k) r.m[k] = x.m[k] + (y.m[k] - x.m[k]) * s;
    world(i) = r;
  }
  computeSkin(skeleton);
}

void Pose::blendInto(const Pose& other, float t) {
  const int n = std::min(count(), other.count());
  const float s = clampf(t, 0.0f, 1.0f);
  for (int i = 0; i < n; ++i) {
    Mat4& x = world_[static_cast<size_t>(i)];
    const Mat4& y = other.world_[static_cast<size_t>(i)];
    for (int k = 0; k < 16; ++k) x.m[k] += (y.m[k] - x.m[k]) * s;
  }
}

bool Pose::finite() const {
  for (const Mat4& m : world_)
    for (int k = 0; k < 16; ++k)
      if (!std::isfinite(m.m[k])) return false;
  return true;
}

// ---------------------------------------------------------------------------
// Weights
// ---------------------------------------------------------------------------
void assignSkinWeights(const Skeleton& skeleton, Vec3 position, const std::vector<int>* rigid, uint8_t joints[4],
                       uint8_t weights[4]) {
  joints[0] = joints[1] = joints[2] = joints[3] = 0;
  weights[0] = weights[1] = weights[2] = weights[3] = 0;
  if (rigid && !rigid->empty()) {
    joints[0] = static_cast<uint8_t>(clampf(static_cast<float>((*rigid)[0]), 0.0f, 255.0f));
    weights[0] = 255;
    return;
  }
  const int n = skeleton.count();
  if (n == 0) return;
  // The four closest bone segments win, weighted by 1/(d^4 + eps): smooth across
  // joints, effectively rigid in the middle of a bone.
  float bestD[4] = {1e30f, 1e30f, 1e30f, 1e30f};
  int bestI[4] = {0, 0, 0, 0};
  for (int i = 0; i < n; ++i) {
    const float a = skeleton.bindWorld(i).position().y;
    (void)a;
    const float d = pointSegmentDistanceSq(position, skeleton.bindWorld(i).position(), skeleton.bindTip(i));
    for (int k = 0; k < 4; ++k) {
      if (d < bestD[k]) {
        for (int m = 3; m > k; --m) {
          bestD[m] = bestD[m - 1];
          bestI[m] = bestI[m - 1];
        }
        bestD[k] = d;
        bestI[k] = i;
        break;
      }
    }
  }
  float w[4] = {0, 0, 0, 0};
  float total = 0.0f;
  for (int k = 0; k < 4; ++k) {
    const float d2 = bestD[k];
    w[k] = d2 > 1e29f ? 0.0f : 1.0f / (d2 * d2 + 1e-6f);
    total += w[k];
  }
  if (!(total > 0.0f)) {
    joints[0] = static_cast<uint8_t>(bestI[0]);
    weights[0] = 255;
    return;
  }
  // Largest-remainder quantisation: the weights sum to exactly 255, and the rounding
  // residue lands on the largest influence (so no entry can be pushed out of 0..255).
  float q[4] = {0, 0, 0, 0};
  int sum = 0;
  int biggest = 0;
  for (int k = 0; k < 4; ++k) {
    q[k] = w[k] / total;
    weights[k] = static_cast<uint8_t>(q[k] * 255.0f + 0.5f);
    sum += weights[k];
    if (q[k] > q[biggest]) biggest = k;
  }
  const int diff = 255 - sum;
  const int fixed = static_cast<int>(weights[biggest]) + diff;
  weights[biggest] = static_cast<uint8_t>(clampf(static_cast<float>(fixed), 0.0f, 255.0f));
  for (int k = 0; k < 4; ++k) joints[k] = static_cast<uint8_t>(bestI[k]);
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
namespace {
SkinVertex makeVertex(Vec3 p, Vec3 n, Vec2 uv, const SkinMaterial& m) {
  SkinVertex v{};
  v.px = p.x; v.py = p.y; v.pz = p.z;
  v.nx = n.x; v.ny = n.y; v.nz = n.z;
  v.u = uv.x; v.v = uv.y;
  v.r = toUnorm8(m.albedo.x);
  v.g = toUnorm8(m.albedo.y);
  v.b = toUnorm8(m.albedo.z);
  v.rough = toUnorm8(m.roughness);
  v.weaveFreq = toUnorm8(m.weaveFreq * 0.5f);
  v.weaveStrength = toUnorm8(m.weaveStrength);
  v.emissive = toUnorm8(m.emissive * 0.125f);
  return v;
}

// Catmull-Rom smoothing of a polyline (positions and radii) so sweeps bend smoothly
// through joints instead of kinking at each bone.
void smoothChain(const std::vector<Vec3>& in, const std::vector<float>& rIn, int subdivisions,
                 std::vector<Vec3>& out, std::vector<float>& rOut) {
  out.clear();
  rOut.clear();
  const int n = static_cast<int>(in.size());
  if (n == 0) return;
  if (n < 3) {
    out = in;
    rOut = rIn;
    return;
  }
  const int steps = std::max(subdivisions, 1);
  for (int i = 0; i < n - 1; ++i) {
    const Vec3 p0 = in[std::max(i - 1, 0)];
    const Vec3 p1 = in[i];
    const Vec3 p2 = in[i + 1];
    const Vec3 p3 = in[std::min(i + 2, n - 1)];
    const float r0 = rIn[static_cast<size_t>(std::max(i - 1, 0))];
    const float r1 = rIn[static_cast<size_t>(i)];
    const float r2 = rIn[static_cast<size_t>(i + 1)];
    const float r3 = rIn[static_cast<size_t>(std::min(i + 2, n - 1))];
    for (int s = 0; s < steps; ++s) {
      const float t = static_cast<float>(s) / static_cast<float>(steps);
      const float t2 = t * t, t3 = t2 * t;
      auto cr = [&](float a, float b, float c, float d) {
        return 0.5f * ((2.0f * b) + (-a + c) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 +
                       (-a + 3.0f * b - 3.0f * c + d) * t3);
      };
      out.push_back(Vec3{cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y), cr(p0.z, p1.z, p2.z, p3.z)});
      rOut.push_back(std::fmax(cr(r0, r1, r2, r3), 0.005f));
    }
  }
  out.push_back(in.back());
  rOut.push_back(rIn.back());
}
}  // namespace

void addSkinSweep(SkinnedMesh& mesh, const Skeleton& skeleton, const std::vector<Vec3>& centre,
                  const std::vector<float>& radii, int sides, const SkinMaterial& material, bool capStart,
                  bool capEnd, int rigidBone, const std::vector<int>* rigid) {
  if (centre.size() < 2 || centre.size() != radii.size() || sides < 3) return;
  std::vector<Vec3> c;
  std::vector<float> r;
  smoothChain(centre, radii, 3, c, r);
  const std::vector<int>* rigidPtr = rigid;
  std::vector<int> single;
  if (rigidBone >= 0) {
    single = {rigidBone};
    rigidPtr = &single;
  }
  const int rings = static_cast<int>(c.size());
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  // Frame transport along the curve (parallel-ish transport using a fixed reference).
  Vec3 prevT = normalize(c[1] - c[0]);
  Vec3 ref = std::fabs(prevT.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  Vec3 x = normalize(cross(ref, prevT));
  for (int i = 0; i < rings; ++i) {
    const Vec3 t = i + 1 < rings ? normalize(c[static_cast<size_t>(i + 1)] - c[static_cast<size_t>(i)]) : prevT;
    // Re-orthogonalise the frame against the new tangent to avoid twisting.
    x = normalize(x - t * dot(x, t));
    if (lengthSq(x) < 1e-6f) {
      ref = std::fabs(t.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
      x = normalize(cross(ref, t));
    }
    const Vec3 y = normalize(cross(t, x));
    prevT = t;
    const float v = static_cast<float>(i) / static_cast<float>(std::max(rings - 1, 1));
    for (int s = 0; s <= sides; ++s) {
      const float a = kTwoPi * static_cast<float>(s) / static_cast<float>(sides);
      const Vec3 n = x * std::cos(a) + y * std::sin(a);
      const Vec3 p = c[static_cast<size_t>(i)] + n * r[static_cast<size_t>(i)];
      SkinVertex vx = makeVertex(p, n, Vec2{static_cast<float>(s) / static_cast<float>(sides), v * 6.0f}, material);
      assignSkinWeights(skeleton, p, rigidPtr, vx.joints, vx.weights);
      mesh.vertices.push_back(vx);
    }
  }
  for (int i = 0; i < rings - 1; ++i)
    for (int s = 0; s < sides; ++s) {
      const uint32_t a = base + static_cast<uint32_t>(i * (sides + 1) + s);
      const uint32_t b = a + 1;
      const uint32_t cc = a + static_cast<uint32_t>(sides + 1);
      const uint32_t d = cc + 1;
      mesh.indices.insert(mesh.indices.end(), {a, cc, b, b, cc, d});
    }
  // Caps: a fan to a centre point.
  auto cap = [&](bool start) {
    const Vec3 tipPos = c[start ? 0 : rings - 1];
    const Vec3 normal = normalize(c[start ? 0 : rings - 1] - c[start ? 1 : rings - 2]);
    SkinVertex vx = makeVertex(tipPos, normal, Vec2{0.5f, 0.5f}, material);
    assignSkinWeights(skeleton, tipPos, rigidPtr, vx.joints, vx.weights);
    const uint32_t centreIndex = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(vx);
    const uint32_t ringBase = base + static_cast<uint32_t>((start ? 0 : rings - 1) * (sides + 1));
    for (int s = 0; s < sides; ++s) {
      const uint32_t a = ringBase + static_cast<uint32_t>(s);
      const uint32_t b = ringBase + static_cast<uint32_t>(s + 1);
      if (start) mesh.indices.insert(mesh.indices.end(), {centreIndex, b, a});
      else mesh.indices.insert(mesh.indices.end(), {centreIndex, a, b});
    }
  };
  if (capStart) cap(true);
  if (capEnd) cap(false);
}

void addSkinEllipsoid(SkinnedMesh& mesh, const Skeleton& skeleton, Vec3 centre, Vec3 radii, int segments, int rings,
                      const SkinMaterial& material, int rigidBone) {
  if (segments < 3 || rings < 2) return;
  std::vector<int> single;
  const std::vector<int>* rigid = nullptr;
  if (rigidBone >= 0) {
    single = {rigidBone};
    rigid = &single;
  }
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  for (int y = 0; y <= rings; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(rings);
    const float phi = v * kPi;
    for (int s = 0; s <= segments; ++s) {
      const float u = static_cast<float>(s) / static_cast<float>(segments);
      const float theta = u * kTwoPi;
      const Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
      const Vec3 p = centre + Vec3{n.x * radii.x, n.y * radii.y, n.z * radii.z};
      SkinVertex vx = makeVertex(p, normalize(Vec3{n.x / std::fmax(radii.x, 1e-4f), n.y / std::fmax(radii.y, 1e-4f),
                                                   n.z / std::fmax(radii.z, 1e-4f)}),
                                 Vec2{u * 2.0f, v * 2.0f}, material);
      assignSkinWeights(skeleton, p, rigid, vx.joints, vx.weights);
      mesh.vertices.push_back(vx);
    }
  }
  for (int y = 0; y < rings; ++y)
    for (int s = 0; s < segments; ++s) {
      const uint32_t a = base + static_cast<uint32_t>(y * (segments + 1) + s);
      const uint32_t b = a + 1;
      const uint32_t c = a + static_cast<uint32_t>(segments + 1);
      const uint32_t d = c + 1;
      mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
    }
}

void addSkinBox(SkinnedMesh& mesh, const Skeleton& skeleton, const Mat4& transform, Vec3 h, const SkinMaterial& m,
                int rigidBone) {
  std::vector<int> single;
  const std::vector<int>* rigid = nullptr;
  if (rigidBone >= 0) {
    single = {rigidBone};
    rigid = &single;
  }
  // Canonical +/-1 cube faces; `h` scales each axis, `transform` places the box.
  static const Vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  for (int f = 0; f < 6; ++f) {
    const Vec3 n = normals[f];
    const Vec3 t = std::fabs(n.y) > 0.5f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 u = normalize(cross(t, n));
    const Vec3 v = cross(n, u);
    for (int k = 0; k < 4; ++k) {
      const float su = (k == 0 || k == 1) ? -1.0f : 1.0f;
      const float sv = (k == 0 || k == 3) ? -1.0f : 1.0f;
      const Vec3 corner = n + u * su + v * sv;              // +/-1 cube corner
      const Vec3 lp{corner.x * h.x, corner.y * h.y, corner.z * h.z};
      const Vec3 wp = transform.transformPoint(lp);
      const Vec3 wn = normalize(transform.transformDir(n));
      SkinVertex vx = makeVertex(wp, wn, Vec2{su * 0.5f + 0.5f, sv * 0.5f + 0.5f}, m);
      assignSkinWeights(skeleton, wp, rigid, vx.joints, vx.weights);
      mesh.vertices.push_back(vx);
    }
    const uint32_t a = base + static_cast<uint32_t>(f * 4);
    mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a, a + 2, a + 3});
  }
}

void addSkinCone(SkinnedMesh& mesh, const Skeleton& skeleton, const Mat4& transform, float radius, float height,
                 int segments, const SkinMaterial& material, int rigidBone) {
  if (segments < 3) return;
  std::vector<int> single;
  const std::vector<int>* rigid = nullptr;
  if (rigidBone >= 0) {
    single = {rigidBone};
    rigid = &single;
  }
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  // Top vertex.
  {
    const Vec3 wp = transform.transformPoint(Vec3{0, height, 0});
    SkinVertex vx = makeVertex(wp, normalize(transform.transformDir(Vec3{0, 1, 0})), Vec2{0.5f, 0.0f}, material);
    assignSkinWeights(skeleton, wp, rigid, vx.joints, vx.weights);
    mesh.vertices.push_back(vx);
  }
  for (int s = 0; s <= segments; ++s) {
    const float a = kTwoPi * static_cast<float>(s) / static_cast<float>(segments);
    const Vec3 n = normalize(Vec3{std::cos(a), 0.35f, std::sin(a)});
    const Vec3 lp{std::cos(a) * radius, 0.0f, std::sin(a) * radius};
    const Vec3 wp = transform.transformPoint(lp);
    SkinVertex vx = makeVertex(wp, normalize(transform.transformDir(n)), Vec2{static_cast<float>(s) / static_cast<float>(segments), 1.0f}, material);
    assignSkinWeights(skeleton, wp, rigid, vx.joints, vx.weights);
    mesh.vertices.push_back(vx);
  }
  for (int s = 0; s < segments; ++s) {
    const uint32_t a = base;
    const uint32_t b = base + 1 + static_cast<uint32_t>(s);
    const uint32_t c = b + 1;
    mesh.indices.insert(mesh.indices.end(), {a, b, c});
  }
}

void SkinnedMesh::computeBounds() {
  boundsMin = Vec3{1e30f, 1e30f, 1e30f};
  boundsMax = Vec3{-1e30f, -1e30f, -1e30f};
  for (const SkinVertex& v : vertices) {
    boundsMin.x = std::fmin(boundsMin.x, v.px);
    boundsMin.y = std::fmin(boundsMin.y, v.py);
    boundsMin.z = std::fmin(boundsMin.z, v.pz);
    boundsMax.x = std::fmax(boundsMax.x, v.px);
    boundsMax.y = std::fmax(boundsMax.y, v.py);
    boundsMax.z = std::fmax(boundsMax.z, v.pz);
  }
}

// ---------------------------------------------------------------------------
// IK
// ---------------------------------------------------------------------------
IkResult solveTwoBoneIk(Vec3 root, Vec3 target, float upperLen, float lowerLen, Vec3 poleDir) {
  IkResult out;
  const float reachMax = upperLen + lowerLen;
  Vec3 toTarget = target - root;
  float dist = length(toTarget);
  if (!(dist > 1e-5f)) {
    toTarget = Vec3{0, -1, 0};
    dist = 1e-5f;
  }
  const Vec3 dir = toTarget / dist;
  // Clamp into the annulus the chain can actually reach (never NaN: both lengths > 0).
  const float minReach = std::fabs(upperLen - lowerLen) + 1e-4f;
  out.reachable = dist <= reachMax && dist >= minReach;
  const float d = clampf(dist, minReach, reachMax);
  out.endPos = root + dir * d;
  // Law of cosines: angle at the root between the chain direction and the upper bone.
  const float cosRoot = clampf((upperLen * upperLen + d * d - lowerLen * lowerLen) / (2.0f * upperLen * d), -1.0f, 1.0f);
  const float rootAngle = std::acos(cosRoot);
  // Bend plane: the pole direction projected perpendicular to the chain direction.
  Vec3 pole = poleDir - dir * dot(poleDir, dir);
  if (lengthSq(pole) < 1e-8f) {
    const Vec3 alt = std::fabs(dir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    pole = alt - dir * dot(alt, dir);
  }
  pole = normalize(pole);
  const float sinA = std::sin(rootAngle), cosA = std::cos(rootAngle);
  out.jointPos = root + (dir * cosA + pole * sinA) * upperLen;
  return out;
}

namespace {
Vec3 rotateAboutAxis(Vec3 v, Vec3 axis, float angle) {
  const Vec3 a = normalize(axis);
  const float c = std::cos(angle), s = std::sin(angle);
  return v * c + cross(a, v) * s + a * (dot(a, v) * (1.0f - c));
}
}  // namespace

IkResult solveTwoBoneIkPlanar(Vec3 root, Vec3 target, float upperLen, float lowerLen, Vec3 bendAxis,
                              float bendSign) {
  IkResult out;
  const float reachMax = upperLen + lowerLen;
  Vec3 toTarget = target - root;
  float dist = length(toTarget);
  if (!(dist > 1e-5f)) {
    toTarget = Vec3{0, -1, 0};
    dist = 1e-5f;
  }
  const Vec3 dir = toTarget / dist;
  const float minReach = std::fabs(upperLen - lowerLen) + 1e-4f;
  out.reachable = dist <= reachMax && dist >= minReach;
  const float d = clampf(dist, minReach, reachMax);
  out.endPos = root + dir * d;
  const float cosRoot = clampf((upperLen * upperLen + d * d - lowerLen * lowerLen) / (2.0f * upperLen * d), -1.0f, 1.0f);
  const float rootAngle = std::acos(cosRoot);
  Vec3 axis = bendAxis - dir * dot(bendAxis, dir);
  if (lengthSq(axis) < 1e-8f) {
    // The limb is parallel to the plane normal (out of plane): fall back to a stable
    // perpendicular so the result stays finite and continuous in practice.
    const Vec3 alt = std::fabs(dir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    axis = alt - dir * dot(alt, dir);
  }
  axis = normalize(axis);
  const float sign = bendSign >= 0.0f ? 1.0f : -1.0f;
  const Vec3 upperDir = rotateAboutAxis(dir, axis, sign * rootAngle);
  out.jointPos = root + upperDir * upperLen;
  return out;
}

Mat4 aimJointStable(Vec3 from, Vec3 to, Vec3 refForward, float roll) {
  const Vec3 dir = normalize(to - from);
  if (lengthSq(dir) < 1e-8f) return Mat4::translation(from);
  const Vec3 up = -dir;
  // Project the reference forward axis perpendicular to the bone, falling back to a
  // perpendicular world axis when they are (nearly) parallel.
  Vec3 ref = refForward - up * dot(refForward, up);
  if (lengthSq(ref) < 1e-6f) {
    const Vec3 alt = std::fabs(up.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    ref = alt - up * dot(alt, up);
  }
  ref = normalize(ref);
  const Vec3 x = normalize(cross(ref, up));
  const Vec3 z = normalize(cross(x, up));
  Mat4 m = Mat4::identity();
  m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
  m.m[4] = up.x; m.m[5] = up.y; m.m[6] = up.z;
  m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
  m.m[12] = from.x; m.m[13] = from.y; m.m[14] = from.z;
  if (roll != 0.0f) {
    Mat4 out = m * Mat4::rotation({0, 1, 0}, roll);
    out.m[12] = from.x; out.m[13] = from.y; out.m[14] = from.z;
    return out;
  }
  return m;
}

Mat4 aimJoint(Vec3 from, Vec3 to, float roll) {
  const Vec3 dir = normalize(to - from);
  if (lengthSq(dir) < 1e-8f) return Mat4::translation(from);
  // Bone convention: joints hang with their bind tip along -Y (limbs point down at
  // bind), so the aim rotation maps -Y onto the requested direction — equivalently
  // +Y onto -dir.
  const Vec3 up = -dir;
  const Vec3 ref = std::fabs(up.y) < 0.999f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 x = normalize(cross(ref, up));
  const Vec3 z = cross(x, up);
  Mat4 m = Mat4::identity();
  m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
  m.m[4] = up.x; m.m[5] = up.y; m.m[6] = up.z;
  m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
  m.m[12] = from.x; m.m[13] = from.y; m.m[14] = from.z;
  if (roll != 0.0f) {
    Mat4 out = m * Mat4::rotation({0, 1, 0}, roll);
    out.m[12] = from.x; out.m[13] = from.y; out.m[14] = from.z;
    return out;
  }
  return m;
}

}  // namespace aaa
