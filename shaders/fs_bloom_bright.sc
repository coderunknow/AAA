$input v_texcoord0, v_dir

#include <bgfx_shader.sh>

// Bloom bright pass: soft-knee extraction of the HDR scene colour into a half-res
// target. The scene is linear HDR; the threshold works in pre-exposure units.
SAMPLER2D(s_hdr, 0);
uniform vec4 u_bright;  // x: threshold, y: knee width, z: scale, w: unused

void main() {
  vec3 c = texture2D(s_hdr, v_texcoord0).rgb;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  // Soft knee: 0 below the threshold, ramping to full contribution one knee above it.
  float w = smoothstep(u_bright.x, u_bright.x + max(u_bright.y, 1e-3), l);
  gl_FragColor = vec4(c * w * u_bright.z, 1.0);
}
