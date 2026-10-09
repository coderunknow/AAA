$input v_texcoord0

#include <bgfx_shader.sh>

// Soft radial falloff; alpha-blended black darkens the ground under a character.
// Drawn depth-tested against the terrain, without depth writes, so it never
// occludes the character itself.
uniform vec4 u_contactParams;  // x: strength

void main() {
  float d = length(v_texcoord0);
  float a = u_contactParams.x * pow(saturate(1.0 - d), 1.6);
  gl_FragColor = vec4(0.0, 0.0, 0.0, a);
}
