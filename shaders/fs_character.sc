$input v_wpos, v_normal, v_texcoord0, v_color0

#include <bgfx_shader.sh>
#include "common.sh"
#include "shadow.sh"

SAMPLER2D(s_detail, 3);
uniform vec4 u_material;  // rgb: albedo, w: roughness
uniform vec4 u_tintA;     // x: weave frequency, y: weave strength

void main() {
  vec3 n = normalize(v_normal);
  vec3 v = normalize(u_camPos.xyz - v_wpos);
  vec4 d = texture2D(s_detail, v_texcoord0 * u_tintA.x);
  vec3 albedo = u_material.rgb * (1.0 - u_tintA.y + u_tintA.y * 2.0 * d.b);
  // Soft rim from the sky keeps the silhouette readable against dark forest.
  float rim = pow(1.0 - saturate1(dot(n, v)), 3.0) * 0.35;
  float sh = sunShadow(v_wpos, n);
  vec3 color = shadeSurface(albedo, n, v, u_material.w, 1.0, sh) + u_skyAmbient.rgb * rim * albedo * 2.0;
  gl_FragColor = vec4(applyFog(color, v_wpos), 1.0);
}
