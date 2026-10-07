$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

void main() {
  v_texcoord0 = vec2_splat(0.0);
  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
