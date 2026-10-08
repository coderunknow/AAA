$input v_texcoord0, v_dir

#include <bgfx_shader.sh>

// Half-resolution screen-space sun shafts: a radial blur of the bright pass
// towards the sun's screen position, so light appears to stream through mist.
// The per-pixel hash dithers the sample positions to hide banding.
SAMPLER2D(s_blur, 0);  // the bright-pass texture
uniform vec4 u_sunScreen;  // xy: sun uv (origin-aware), z: strength, w: tap count
uniform vec4 u_texel;      // xy: half-res texel size (uv units)

void main() {
  vec2 uv = v_texcoord0;
  vec2 toSun = u_sunScreen.xy - uv;
  float dist = length(toSun);
  // Dither the ray start to break up banding on smooth gradients.
  float h = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  vec2 delta = toSun / max(u_sunScreen.w, 1.0);
  float decay = 0.965;
  float w = 1.0;
  vec3 acc = vec3_splat(0.0);
  float weightSum = 0.0;
  for (int i = 0; i < 64; ++i) {
    if (float(i) >= u_sunScreen.w) break;
    vec2 suv = uv + delta * (float(i) + h);
    acc += texture2D(s_blur, suv).rgb * w;
    weightSum += w;
    w *= decay;
  }
  vec3 shafts = weightSum > 0.0 ? acc / weightSum : vec3_splat(0.0);
  // Fade the whole effect with distance from the sun so the sky near the sun
  // does not blow out, and kill it entirely when the sun is off-screen/behind.
  float falloff = exp(-dist * 1.35);
  gl_FragColor = vec4(shafts * (falloff * u_sunScreen.z), 1.0);
}
