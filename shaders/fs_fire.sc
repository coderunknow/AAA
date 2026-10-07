$input v_wpos, v_texcoord0, v_color0

#include <bgfx_shader.sh>
#include "common.sh"

SAMPLER2D(s_detail, 3);

// Black-body-ish ramp: deep red -> orange -> pale yellow.
vec3 fireRamp(float h) {
  vec3 c = mix(vec3(0.5, 0.04, 0.0), vec3(1.0, 0.32, 0.03), smoothstep(0.0, 0.45, h));
  c = mix(c, vec3(1.0, 0.75, 0.35), smoothstep(0.45, 0.85, h));
  return mix(c, vec3(1.0, 0.95, 0.8), smoothstep(0.85, 1.0, h));
}

void main() {
  float type = v_color0.x;
  float seed = v_color0.y;
  float I = v_color0.w;
  float t = u_camPos.w;
  vec2 uv = v_texcoord0;
  vec3 color;
  if (type < 0.5) {
    // Teardrop tongue whose edge is eroded by upward-scrolling noise.
    float y = uv.y;
    float n = texture2D(s_detail, vec2(uv.x * 0.18 + seed, y * 0.35 - t * 0.9)).r;
    float n2 = texture2D(s_detail, vec2(uv.x * 0.4 - seed * 0.7, y * 0.7 - t * 1.7)).g;
    float width = pow(max(1.0 - y, 0.0), 0.8) * sqrt(max(y, 0.0) + 0.05) * 1.45;
    float edge = abs(uv.x) + (n * 0.55 + n2 * 0.35 - 0.45) * (0.4 + y);
    float mask = 1.0 - smoothstep(width * 0.55, width, edge);
    float heat = mask * (1.0 - y * 0.85) * (0.7 + 0.5 * n2);
    color = fireRamp(clamp(heat, 0.0, 1.0)) * mask * (2.2 + 4.0 * heat) * I;
  } else if (type < 1.5) {
    float d = length(vec2(uv.x, uv.y * 2.0 - 1.0));
    color = vec3(1.0, 0.55, 0.15) * (1.0 - smoothstep(0.2, 1.0, d)) * v_color0.z * 7.0;
  } else {
    float d = length(vec2(uv.x, (uv.y - 0.15) * 1.4));
    float glow = pow(max(1.0 - d, 0.0), 2.5);
    color = vec3(1.0, 0.38, 0.1) * glow * 0.35 * I;
  }
  // Additive: attenuate by fog transmittance only (fog colour is already in the frame).
  vec3 fogged = applyFog(color, v_wpos) - applyFog(vec3_splat(0.0), v_wpos);
  gl_FragColor = vec4(max(fogged, vec3_splat(0.0)), 1.0);
}
