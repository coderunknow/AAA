$input v_wpos, v_texcoord0

#include <bgfx_shader.sh>
#include "common.sh"
#include "shadow.sh"

SAMPLER2D(s_detail, 3);
uniform vec4 u_skyZenith;
uniform vec4 u_skyHorizon;

void main() {
  vec3 wpos = v_wpos;
  float t = u_camPos.w;
  float along = v_texcoord0.y;
  float across = v_texcoord0.x;
  // Ripples: two detail layers scrolling downstream at different speeds and scales.
  vec2 uvA = vec2(across * 1.6, along * 0.22 - t * 0.55);
  vec2 uvB = vec2(across * 3.1 + 0.37, along * 0.47 - t * 0.9);
  float e = 1.0 / 128.0;
  float hA = texture2D(s_detail, uvA).r, hAx = texture2D(s_detail, uvA + vec2(e, 0.0)).r, hAy = texture2D(s_detail, uvA + vec2(0.0, e)).r;
  float hB = texture2D(s_detail, uvB).b, hBx = texture2D(s_detail, uvB + vec2(e, 0.0)).b, hBy = texture2D(s_detail, uvB + vec2(0.0, e)).b;
  vec3 n = normalize(vec3((hA - hAx) * 2.2 + (hB - hBx) * 1.2, 1.0, (hA - hAy) * 2.2 + (hB - hBy) * 1.2));

  vec3 v = normalize(u_camPos.xyz - wpos);
  float cosT = saturate1(dot(n, v));
  float fresnel = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);

  // Sky reflection (matches the sky gradient; mist colour toward the horizon).
  vec3 r = reflect(-v, n);
  vec3 sky = mix(u_skyHorizon.rgb, u_skyZenith.rgb, pow(saturate1(r.y), 0.6));
  sky = mix(u_fogColor.rgb, sky, 0.6);
  float sh = sunShadow(wpos, vec3(0.0, 1.0, 0.0));
  vec3 spec = u_sunColor.rgb * pow(saturate1(dot(r, u_sunDir.xyz)), 350.0) * 6.0 * sh;

  // Body: dark, tannin-tinted mountain water, lifted by ambient + a little sun scatter.
  vec3 body = vec3(0.018, 0.032, 0.026) * (u_skyAmbient.rgb * 2.0 + u_sunColor.rgb * 0.25 * sh);
  vec3 color = mix(body, sky, fresnel) + spec;
  // Faint foam streaks where the channel narrows to the banks.
  float foam = smoothstep(0.62, 0.95, hB) * smoothstep(0.45, 0.85, abs(across));
  color = mix(color, (u_skyAmbient.rgb + u_sunColor.rgb * sh * 0.4) * 0.6, foam * 0.35);
  float alpha = mix(0.78, 0.97, fresnel);
  gl_FragColor = vec4(applyFog(color, wpos), alpha);
}
