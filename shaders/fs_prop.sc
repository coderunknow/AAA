$input v_wpos, v_normal, v_texcoord0, v_color0, v_extra

#include <bgfx_shader.sh>
#include "common.sh"
#include "shadow.sh"

SAMPLER2D(s_detail, 3);
uniform vec4 u_material;  // x: material id (0 bark, 1 rock, 2 limestone, 3 bamboo, 4 log)
uniform vec4 u_tintA;     // colour variation endpoints
uniform vec4 u_tintB;

void main() {
  vec3 wpos = v_wpos;
  vec3 n = normalize(v_normal);
  float id = u_material.x;
  float tint = v_extra.x;
  float ao = v_color0.r;
  vec3 base = mix(u_tintA.rgb, u_tintB.rgb, tint);
  vec3 albedo = base;
  float roughness = 0.9;
  vec3 v = normalize(u_camPos.xyz - wpos);

  if (id < 0.5) {
    // Bark: deep vertical fissures, plates, moss creeping up the shaded side.
    vec4 d = texture2D(s_detail, v_texcoord0 * vec2(1.0, 0.18));
    vec4 d2 = texture2D(s_detail, v_texcoord0 * vec2(3.0, 0.7) + vec2(0.3, 0.1));
    float fissure = smoothstep(0.35, 0.75, d.g + d2.r * 0.3);
    albedo = base * (0.55 + 0.6 * d2.b) * (1.0 - 0.6 * fissure);
    float mossW = smoothstep(0.55, 0.95, n.y * 0.4 + d.a + (1.0 - smoothstep(0.0, 3.0, v_extra.z)) * 0.45);
    albedo = mix(albedo, vec3(0.06, 0.09, 0.03), mossW * 0.8);
    ao *= 1.0 - 0.5 * fissure;
  } else if (id < 1.5) {
    // Mossy rock: triplanar grain, moss on upward faces.
    vec3 w = pow(abs(n), vec3_splat(4.0)); w /= (w.x + w.y + w.z);
    vec4 d = texture2D(s_detail, wpos.zy * 0.35) * w.x + texture2D(s_detail, wpos.xz * 0.35) * w.y + texture2D(s_detail, wpos.xy * 0.35) * w.z;
    albedo = base * (0.6 + 0.6 * d.r) * (1.0 - 0.55 * d.g);
    float mossW = smoothstep(0.35, 0.8, n.y + (d.a - 0.5) * 0.8);
    albedo = mix(albedo, mix(vec3(0.05, 0.08, 0.025), vec3(0.11, 0.14, 0.04), d.b), mossW);
    roughness = mix(0.8, 0.95, mossW);
  } else if (id < 2.5) {
    // Limestone spire: pale weathered stone, dark rain streaks, green ledges.
    vec4 d = texture2D(s_detail, vec2(v_texcoord0.x * 0.25, wpos.y * 0.04));
    vec4 d2 = texture2D(s_detail, wpos.xz * 0.2 + vec2(wpos.y * 0.07, 0.0));
    float streak = smoothstep(0.4, 0.8, texture2D(s_detail, vec2(v_texcoord0.x * 0.6, wpos.y * 0.01)).r);
    albedo = base * (0.65 + 0.5 * d.r) * (1.0 - 0.45 * streak) * (1.0 - 0.4 * d2.g);
    float ledge = smoothstep(0.55, 0.85, n.y + (d2.a - 0.5) * 0.5);
    albedo = mix(albedo, vec3(0.05, 0.09, 0.03), ledge);
    roughness = 0.85;
  } else if (id < 3.5) {
    // Bamboo culm: green/yellow with pale nodes and a waxy sheen.
    float node = smoothstep(0.92, 1.0, fract(v_texcoord0.y * 0.95));
    vec4 d = texture2D(s_detail, v_texcoord0 * vec2(0.5, 0.05));
    albedo = base * (0.8 + 0.4 * d.r);
    albedo = mix(albedo, vec3(0.32, 0.3, 0.18), node * 0.8);
    roughness = 0.45;
  } else {
    // Fallen log: decayed, mostly moss-covered on top.
    vec4 d = texture2D(s_detail, v_texcoord0 * vec2(1.0, 0.3));
    albedo = base * (0.5 + 0.5 * d.b) * (1.0 - 0.5 * smoothstep(0.4, 0.8, d.g));
    float mossW = smoothstep(0.1, 0.6, n.y + (d.a - 0.5));
    albedo = mix(albedo, vec3(0.06, 0.1, 0.03), mossW * 0.9);
  }

  float sh = sunShadow(wpos, n);
  vec3 color = shadeSurface(albedo, n, v, roughness, ao, sh);
  gl_FragColor = vec4(applyFog(color, wpos), 1.0);
}
