$input v_wpos, v_texcoord0

#include <bgfx_shader.sh>
#include "common.sh"
#include "shadow.sh"

SAMPLER2D(s_normalmap, 1);    // rg: packed normal xz
SAMPLER2D(s_terrainMask, 2);  // r: wetness, g: trail, b: forest litter, a: grass
SAMPLER2D(s_detail, 3);       // r: fbm, g: cracks, b: fine grain, a: blotches (tileable)

vec3 triplanarDetail(vec3 _p, vec3 _n, float _scale) {
  vec3 w = pow(abs(_n), vec3_splat(4.0));
  w /= (w.x + w.y + w.z);
  vec3 x = texture2D(s_detail, _p.zy * _scale).rgb;
  vec3 y = texture2D(s_detail, _p.xz * _scale).rgb;
  vec3 z = texture2D(s_detail, _p.xy * _scale).rgb;
  return x * w.x + y * w.y + z * w.z;
}

void main() {
  vec3 wpos = v_wpos;
  vec2 nm = texture2D(s_normalmap, v_texcoord0).xy * 2.0 - 1.0;
  vec3 n = normalize(vec3(nm.x, sqrt(max(1.0 - dot(nm, nm), 0.0)), nm.y));
  vec4 mask = texture2D(s_terrainMask, v_texcoord0);

  // Multi-scale detail to break tiling: large (14 m), medium (3.5 m), fine (0.8 m).
  vec4 dLarge = texture2D(s_detail, wpos.xz * (1.0 / 14.0));
  vec4 dMed = texture2D(s_detail, wpos.xz * (1.0 / 3.5) + vec2(0.37, 0.71));
  vec4 dFine = texture2D(s_detail, wpos.xz * (1.0 / 0.8));
  float macro = texture2D(s_detail, wpos.xz * (1.0 / 160.0)).a;

  // Micro normal from the fine detail gradient.
  float e = 1.0 / 256.0;
  float h0 = texture2D(s_detail, wpos.xz * (1.0 / 1.6)).r;
  float hx = texture2D(s_detail, wpos.xz * (1.0 / 1.6) + vec2(e, 0.0)).r;
  float hz = texture2D(s_detail, wpos.xz * (1.0 / 1.6) + vec2(0.0, e)).r;
  vec3 micro = vec3(h0 - hx, 0.0, h0 - hz) * 6.0;

  float slope = 1.0 - n.y;
  float wet = mask.r;
  float trail = mask.g;
  float litter = mask.b;
  float grass = mask.a;

  // Layer albedos (linear).
  vec3 moss = mix(vec3(0.035, 0.06, 0.022), vec3(0.075, 0.11, 0.035), dMed.r);
  vec3 grassC = mix(vec3(0.05, 0.08, 0.026), vec3(0.10, 0.125, 0.045), dLarge.a);
  vec3 litterC = mix(vec3(0.075, 0.045, 0.025), vec3(0.15, 0.085, 0.045), dMed.b);  // rust-brown pine needles
  vec3 dirt = mix(vec3(0.085, 0.065, 0.045), vec3(0.16, 0.125, 0.085), dFine.r);  // packed earth path
  vec3 mud = vec3(0.05, 0.04, 0.03);
  vec3 tri = triplanarDetail(wpos, n, 1.0 / 5.0);
  vec3 rock = mix(vec3(0.14, 0.14, 0.135), vec3(0.30, 0.29, 0.27), tri.r) * (1.0 - 0.5 * tri.g);
  rock = mix(rock, vec3(0.09, 0.11, 0.05), smoothstep(0.55, 0.75, tri.b) * 0.5);  // lichen

  vec3 albedo = mix(moss, grassC, saturate1(grass * 1.2 + (macro - 0.5) * 0.6));
  albedo = mix(albedo, litterC, saturate1(litter * (0.6 + 0.6 * dMed.a)));
  albedo = mix(albedo, dirt, saturate1(trail * (0.75 + 0.5 * dFine.b)));
  float rockW = smoothstep(0.24, 0.42, slope + (dLarge.r - 0.5) * 0.18);
  albedo = mix(albedo, rock, rockW);
  albedo *= 0.85 + 0.3 * macro;

  // Wetness near the stream: darker, smoother, mud and gravel at the water line.
  float roughness = mix(0.92, 0.85, rockW);
  albedo = mix(albedo, mix(mud, vec3(0.14, 0.13, 0.11), dFine.g + dMed.r * 0.4), smoothstep(0.55, 0.95, wet));
  albedo *= 1.0 - 0.45 * wet;
  roughness = mix(roughness, 0.28, wet * wet);

  vec3 nn = normalize(n + micro * (1.0 - wet * 0.7));
  vec3 v = normalize(u_camPos.xyz - wpos);
  float ao = mix(0.75, 1.0, dFine.r) * mix(1.0, 0.8, litter);
  float sh = sunShadow(wpos, n);
  vec3 color = shadeSurface(albedo, nn, v, roughness, ao, sh) + localLight(albedo, nn, wpos) * ao;
  gl_FragColor = vec4(applyFog(color, wpos), 1.0);
}
