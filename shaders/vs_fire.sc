$input a_position, a_texcoord0
$output v_wpos, v_texcoord0, v_color0

// Campfire billboards. a_position: xy = quad corner (x in [-1,1], y in [0,1]), z = seed.
// a_texcoord0: x = type (0 flame tongue, 1 spark, 2 ground glow), y = index.
#include <bgfx_shader.sh>
#include "common.sh"

uniform vec4 u_fireParams;  // xyz: fire base (world), w: intensity 0..1

void main() {
  vec2 corner = a_position.xy;
  float seed = a_position.z;
  float type = a_texcoord0.x;
  float t = u_camPos.w;
  float I = u_fireParams.w;
  vec3 base = u_fireParams.xyz;
  vec3 toCam = u_camPos.xyz - base;
  vec3 fwd = normalize(vec3(toCam.x, 0.0, toCam.z) + vec3(1e-4, 0.0, 0.0));
  vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd));
  vec3 up = vec3(0.0, 1.0, 0.0);
  vec3 wpos;
  float life = 1.0;
  if (type < 0.5) {
    // Flame tongues: clustered, each swaying and breathing on its own phase.
    float ang = seed * 6.2831;
    vec3 offs = (right * cos(ang) * 0.12 + fwd * sin(ang) * 0.12) * (0.4 + 0.6 * fract(seed * 7.3));
    float h = (0.55 + 0.5 * fract(seed * 3.7)) * (0.45 + 0.75 * I) * (0.85 + 0.15 * sin(t * 6.0 + seed * 40.0));
    float w = (0.2 + 0.1 * fract(seed * 5.1)) * (0.6 + 0.5 * I);
    float sway = sin(t * 3.1 + seed * 17.0) * 0.07 * corner.y * corner.y;
    wpos = base + offs + right * (corner.x * w + sway) + up * (corner.y * h) - fwd * 0.02 * seed;
  } else if (type < 1.5) {
    // Sparks: rise, drift and fade on independent lifetimes.
    float rate = 0.35 + 0.4 * fract(seed * 13.7);
    life = fract(t * rate + seed * 3.17);
    float a = seed * 47.0 + life * 2.0;
    vec3 drift = vec3(sin(a), 0.0, cos(a * 1.3)) * (0.15 + 0.5 * life) * life + vec3(u_wind.x, 0.0, u_wind.y) * life * 0.6;
    vec3 c = base + drift + up * (0.3 + life * (1.6 + 1.2 * fract(seed * 9.1)));
    float s = 0.018 * (1.0 - life * 0.6);
    wpos = c + right * corner.x * s + up * (corner.y - 0.5) * s * 2.0;
    life = (1.0 - life) * step(0.25, fract(seed * 5.3 + floor(t * rate + seed * 3.17) * 0.37)) * I;
  } else {
    // Soft glow hugging the ground under the fire (fakes light scattering off smoke and ash).
    wpos = base + right * corner.x * 1.4 + up * (corner.y * 1.2 - 0.1);
  }
  v_wpos = wpos;
  v_texcoord0 = corner;
  v_color0 = vec4(type, seed, life, I);
  gl_Position = mul(u_viewProj, vec4(wpos, 1.0));
}
