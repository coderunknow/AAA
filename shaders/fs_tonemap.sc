$input v_texcoord0, v_dir

#include <bgfx_shader.sh>
#include "common.sh"

SAMPLER2D(s_hdr, 0);
uniform vec4 u_post;   // x: exposure, y: saturation, z: vignette, w: contrast
uniform vec4 u_grade;  // rgb: highlight tint, w: shadow tint strength

void main() {
  vec3 hdr = texture2D(s_hdr, v_texcoord0).rgb * u_post.x;
  // Scotopic shift: in very dim light colour vision fades toward a cool blue-grey, so moonlit
  // foliage stops reading as saturated green while firelit areas (brighter) keep their warmth.
  float lumHdr = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
  float photopic = smoothstep(0.01, 0.10, lumHdr);
  hdr = mix(vec3(0.72, 0.86, 1.12) * lumHdr * 1.1, hdr, photopic);
  vec3 c = tonemapACES(hdr);
  // Gentle split-tone: cool shadows, warm highlights.
  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c *= mix(vec3(0.94, 0.98, 1.04), u_grade.rgb, smoothstep(0.1, 0.8, luma));
  c = mix(vec3_splat(luma), c, u_post.y);
  c = clamp((c - 0.5) * u_post.w + 0.5, 0.0, 1.0);
  vec2 q = v_texcoord0 - 0.5;
  c *= 1.0 - u_post.z * dot(q, q) * 1.6;
  c = pow(c, vec3_splat(1.0 / 2.2));
  // Triangular dither: avoids banding in fog and sky gradients.
  float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
  float n2 = fract(sin(dot(gl_FragCoord.xy + 0.37, vec2(39.3468, 11.135))) * 24634.6345);
  c += (n + n2 - 1.0) / 255.0;
  gl_FragColor = vec4(c, 1.0);
}
