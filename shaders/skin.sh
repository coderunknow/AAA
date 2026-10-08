// Shared GPU skinning (PROMPT §10.5): a joint palette of up to 32 matrices, four
// influences per vertex, weights packed as unorm8 that sum to 255.
//
// Joints are supplied in WORLD space (the animator bakes the character's world
// transform into every joint matrix), so skinned positions and normals come out in
// world space directly — which is what all the scene shaders and the shadow pass use.
#define SKIN_MAX_JOINTS 32

uniform mat4 u_joints[SKIN_MAX_JOINTS];

// Decodes the packed attributes into a 4x4-ish blend and applies it.
vec4 skinPosition(vec4 _pos, vec4 _joints, vec4 _weights) {
  vec4 acc = vec4_splat(0.0);
  for (int i = 0; i < 4; ++i) {
    float w = _weights[i];
    if (w <= 0.0) continue;
    int j = int(_joints[i] + 0.5);
    acc += mul(u_joints[j], _pos) * w;
  }
  return acc;
}

vec3 skinNormal(vec3 _normal, vec4 _joints, vec4 _weights) {
  vec3 acc = vec3_splat(0.0);
  for (int i = 0; i < 4; ++i) {
    float w = _weights[i];
    if (w <= 0.0) continue;
    int j = int(_joints[i] + 0.5);
    acc += mul(u_joints[j], vec4(_normal, 0.0)).xyz * w;
  }
  return acc;
}
