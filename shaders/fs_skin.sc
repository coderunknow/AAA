$input v_wpos, v_normal, v_texcoord0, v_color0, v_color1

#include <bgfx_shader.sh>
#include "common.sh"
#include "shadow.sh"

SAMPLER2D(s_detail, 3);
// v_color0: rgb albedo, a roughness. v_color1: weave frequency, weave strength, emissive.

void main() {
  vec3 n = normalize(v_normal);
  vec3 v = normalize(u_camPos.xyz - v_wpos);
  vec3 albedo = v_color0.rgb;
  float weaveFreq = v_color1.x;
  float weaveStrength = v_color1.y;
  if (weaveStrength > 0.001) {
    vec4 d = texture2D(s_detail, v_texcoord0 * weaveFreq);
    albedo *= (1.0 - weaveStrength + weaveStrength * 2.0 * d.b);
  }
  // Soft sky rim keeps the silhouette readable against the dark forest.
  float rim = pow(1.0 - saturate1(dot(n, v)), 3.0) * 0.35;
  float sh = sunShadow(v_wpos, n);
  vec3 color = shadeSurface(albedo, n, v, v_color0.a, 1.0, sh) + u_skyAmbient.rgb * rim * albedo * 2.0;
  color += localLight(albedo, n, v_wpos) + albedo * v_color1.z;
  gl_FragColor = vec4(applyFog(color, v_wpos), 1.0);
}
