$input v_texcoord0, v_dir

#include <bgfx_shader.sh>

// Half-resolution SSAO from the scene depth buffer (normals are derived from the
// depth gradient, so no extra G-buffer is needed). Tangent-space hemisphere kernel,
// rotated per pixel by a hash; the result is a single-channel occlusion term.
// View space is left-handed: the camera looks down +Z, so larger z = farther.
SAMPLER2D(s_depth, 0);
uniform mat4 u_ssaoProj;      // scene projection (NOT bgfx's built-in u_proj)
uniform mat4 u_ssaoInvProj;   // its inverse (clip -> view)
uniform vec4 u_screenParams;  // x: 1 if render-target origin is bottom-left
uniform vec4 u_ssaoParams;    // x: radius (view units), y: bias, z: intensity, w: 1 if depth is homogeneous
uniform vec4 u_texel;         // xy: half-res texel size (uv units)
uniform vec4 u_kernel[16];    // xyz: tangent-space sample offset, w: weight

vec3 reconstructViewPos(vec2 _uv, float _depth) {
  float ndcY = u_screenParams.x > 0.5 ? _uv.y * 2.0 - 1.0 : 1.0 - _uv.y * 2.0;
  float ndcZ = u_ssaoParams.w > 0.5 ? _depth * 2.0 - 1.0 : _depth;
  vec4 v = mul(u_ssaoInvProj, vec4(_uv.x * 2.0 - 1.0, ndcY, ndcZ, 1.0));
  return v.xyz / v.w;
}

void main() {
  vec2 uv = v_texcoord0;
  vec2 duv = u_texel.xy;
  float depth = texture2D(s_depth, uv).r;
  vec3 pos = reconstructViewPos(uv, depth);

  // View-space normal from the depth gradient (screen-space derivative trick).
  vec3 posX = reconstructViewPos(uv + vec2(duv.x, 0.0), texture2D(s_depth, uv + vec2(duv.x, 0.0)).r);
  vec3 posY = reconstructViewPos(uv + vec2(0.0, duv.y), texture2D(s_depth, uv + vec2(0.0, duv.y)).r);
  vec3 n = normalize(cross(posX - pos, posY - pos));
  if (n.z > 0.0) n = -n;  // visible surfaces face the camera (negative view z)

  // Tangent frame around the normal, then a per-pixel rotation from a hash
  // (cheap 4x4-rotating-noise substitute that hides banding).
  vec3 up = abs(n.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
  vec3 tangent = normalize(cross(up, n));
  vec3 bitangent = cross(n, tangent);
  mat3 tbn = mat3(tangent, bitangent, n);
  float angle = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
  float ca = cos(angle), sa = sin(angle);
  mat3 rot = mat3(ca, -sa, 0.0, sa, ca, 0.0, 0.0, 0.0, 1.0);

  float occlusion = 0.0;
  for (int i = 0; i < 16; ++i) {
    vec3 samplePos = pos + tbn * (rot * u_kernel[i].xyz) * u_ssaoParams.x;
    // Project the sample to screen space and read the actual scene depth there.
    vec4 pc = mul(u_ssaoProj, vec4(samplePos, 1.0));
    pc /= pc.w;
    vec2 suv = pc.xy * 0.5 + 0.5;
    suv.y = u_screenParams.x > 0.5 ? suv.y : 1.0 - suv.y;
    float actualZ = reconstructViewPos(suv, texture2D(s_depth, suv).r).z;
    // The sample is occluded when actual geometry is closer than the sample point.
    float rangeCheck = smoothstep(0.0, 1.0, u_ssaoParams.x / max(abs(pos.z - actualZ), 1e-3));
    occlusion += (actualZ < samplePos.z - u_ssaoParams.y ? 1.0 : 0.0) * u_kernel[i].w * rangeCheck;
  }
  float ao = clamp(1.0 - occlusion * u_ssaoParams.z, 0.0, 1.0);
  gl_FragColor = vec4(vec3(ao), 1.0);
}
