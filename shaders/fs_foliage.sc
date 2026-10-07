$input v_wpos, v_normal, v_texcoord0, v_color0, v_extra

#include <bgfx_shader.sh>
#include "common.sh"
#include "shadow.sh"

SAMPLER2D(s_foliage, 0);
uniform vec4 u_material;  // y: alpha reference, z: translucency
uniform vec4 u_tintA;
uniform vec4 u_tintB;

void main() {
  vec4 texel = texture2D(s_foliage, v_texcoord0);
  if (texel.a < u_material.y) discard;
  vec3 wpos = v_wpos;
  vec3 n = normalize(v_normal);
  vec3 v = normalize(u_camPos.xyz - wpos);
  // Two-sided: flip toward the viewer.
  if (dot(n, v) < 0.0) n = -n * 0.6 + vec3(0.0, 0.4, 0.0);
  vec3 tint = mix(u_tintA.rgb, u_tintB.rgb, v_extra.x);
  vec3 albedo = texel.rgb * tint;
  float ao = v_color0.r;
  float sh = sunShadow(wpos, normalize(v_normal));
  vec3 color = shadeFoliage(albedo, n, v, ao, sh * (0.35 + 0.65 * ao), u_material.z);
  gl_FragColor = vec4(applyFog(color, wpos), 1.0);
}
