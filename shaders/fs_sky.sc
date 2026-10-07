$input v_texcoord0, v_dir

#include <bgfx_shader.sh>
#include "common.sh"

SAMPLER2D(s_detail, 3);
uniform vec4 u_skyZenith;   // rgb zenith colour
uniform vec4 u_skyHorizon;  // rgb horizon colour, w: sun disc intensity

void main() {
  vec3 dir = normalize(v_dir);
  float up = dir.y;
  vec3 sky = mix(u_skyHorizon.rgb, u_skyZenith.rgb, pow(saturate1(up), 0.45));
  // Below the horizon blends into the mist colour (distant valleys).
  sky = mix(sky, u_fogColor.rgb, smoothstep(0.08, -0.12, up));
  float mu = dot(dir, u_sunDir.xyz);
  // Mie forward-scattering glow + sun disc.
  sky += u_sunColor.rgb * (0.06 * pow(saturate1(mu), 8.0) + 0.25 * pow(saturate1(mu), 64.0));
  sky += u_sunColor.rgb * u_skyHorizon.w * smoothstep(0.9993, 0.9997, mu);
  // High soft clouds projected on a dome.
  if (up > 0.0) {
    vec2 cuv = dir.xz / (up + 0.12) * 0.18 + u_camPos.w * vec2(0.0015, 0.0006);
    float c = texture2D(s_detail, cuv).r * 0.65 + texture2D(s_detail, cuv * 2.7 + 0.3).a * 0.35;
    float cover = smoothstep(0.48, 0.78, c) * smoothstep(0.0, 0.25, up);
    vec3 cloudCol = u_skyAmbient.rgb * 1.6 + u_sunColor.rgb * (0.12 + 0.35 * pow(saturate1(mu), 4.0));
    sky = mix(sky, cloudCol, cover * 0.75);
  }
  // Same aerial mist as the terrain at the horizon so mountains dissolve into the sky.
  float horizonMist = exp(-max(up, 0.0) * 9.0);
  sky = mix(sky, u_fogColor.rgb + u_sunColor.rgb * pow(saturate1(mu), 6.0) * u_fogParams.z, horizonMist * 0.85);
  gl_FragColor = vec4(sky, 1.0);
}
