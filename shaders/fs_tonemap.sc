$input v_texcoord0, v_dir

#include <bgfx_shader.sh>
#include "common.sh"

SAMPLER2D(s_hdr, 0);
SAMPLER2D(s_bloom, 1);    // half-res bloom (linear-upsampled)
SAMPLER2D(s_shafts, 2);   // half-res sun shafts
SAMPLER2D(s_ssao, 3);     // half-res ambient occlusion (r)
uniform vec4 u_post;   // x: exposure, y: saturation, z: vignette, w: contrast
uniform vec4 u_grade;  // rgb: highlight tint, w: shadow tint strength
uniform vec4 u_effects;    // x: bloom strength, y: shafts strength, z: ssao min, w: fxaa on
uniform vec4 u_texel;      // xy: scene texel size (uv units)

float lumaOf(vec3 _c) { return dot(_c, vec3(0.2126, 0.7152, 0.0722)); }

// Full grading + filmic (ACES) response, factored out so FXAA can grade its
// neighbour samples identically.
vec3 grade(vec3 _hdr) {
  // Scotopic shift: in very dim light colour vision fades toward a cool blue-grey, so moonlit
  // foliage stops reading as saturated green while firelit areas (brighter) keep their warmth.
  float lumHdr = lumaOf(_hdr);
  float photopic = smoothstep(0.01, 0.10, lumHdr);
  vec3 hdr = mix(vec3(0.72, 0.86, 1.12) * lumHdr * 1.1, _hdr, photopic);
  vec3 c = tonemapACES(hdr);
  // Gentle split-tone: cool shadows, warm highlights.
  float luma = lumaOf(c);
  c *= mix(vec3(0.94, 0.98, 1.04), u_grade.rgb, smoothstep(0.1, 0.8, luma));
  c = mix(vec3_splat(luma), c, u_post.y);
  c = clamp((c - 0.5) * u_post.w + 0.5, 0.0, 1.0);
  return c;
}

void main() {
  vec3 hdr = texture2D(s_hdr, v_texcoord0).rgb;
  // Bloom and shafts are linear-light contributions: add them before the response curve.
  hdr += texture2D(s_bloom, v_texcoord0).rgb * u_effects.x;
  hdr += texture2D(s_shafts, v_texcoord0).rgb * u_effects.y;
  hdr *= u_post.x;
  vec3 c = grade(hdr);
  // Ambient occlusion multiplies the final colour (screen-space approximation of
  // per-surface AO; the sky and far fog reconstruct as unoccluded).
  float ao = texture2D(s_ssao, v_texcoord0).r;
  c *= mix(u_effects.z, 1.0, ao);
  // FXAA-lite: luma-based edge detection with a direction-aware blend.
  if (u_effects.w > 0.5) {
    vec2 t = u_texel.xy;
    vec3 cN = grade(texture2D(s_hdr, v_texcoord0 + vec2(0.0, t.y)).rgb * u_post.x);
    vec3 cS = grade(texture2D(s_hdr, v_texcoord0 - vec2(0.0, t.y)).rgb * u_post.x);
    vec3 cE = grade(texture2D(s_hdr, v_texcoord0 + vec2(t.x, 0.0)).rgb * u_post.x);
    vec3 cW = grade(texture2D(s_hdr, v_texcoord0 - vec2(t.x, 0.0)).rgb * u_post.x);
    float lC = lumaOf(c), lN = lumaOf(cN), lS = lumaOf(cS), lE = lumaOf(cE), lW = lumaOf(cW);
    float lMin = min(lC, min(min(lN, lS), min(lE, lW)));
    float lMax = max(lC, max(max(lN, lS), max(lE, lW)));
    float range = lMax - lMin;
    if (range >= max(0.0625, lMax * 0.125)) {
      // The edge is smooth along its own direction: blend the centre towards the
      // average of the two samples taken along that direction.
      vec2 dir = abs(lN - lS) > abs(lE - lW) ? vec2(0.0, t.y) : vec2(t.x, 0.0);
      vec3 cA = grade(texture2D(s_hdr, v_texcoord0 + dir).rgb * u_post.x);
      vec3 cB = grade(texture2D(s_hdr, v_texcoord0 - dir).rgb * u_post.x);
      c = mix(c, (cA + cB) * 0.5, 0.5);
    }
  }
  vec2 q = v_texcoord0 - 0.5;
  c *= 1.0 - u_post.z * dot(q, q) * 1.6;
  c = pow(c, vec3_splat(1.0 / 2.2));
  // Triangular dither: avoids banding in fog and sky gradients.
  float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
  float n2 = fract(sin(dot(gl_FragCoord.xy + 0.37, vec2(39.3468, 11.135))) * 24634.6347);
  c += (n + n2 - 1.0) / 255.0;
  gl_FragColor = vec4(c, 1.0);
}
