$input a_position, a_texcoord0
$output v_wpos, v_texcoord0

#include <bgfx_shader.sh>
#include "common.sh"

void main() {
  v_wpos = a_position;
  v_texcoord0 = a_texcoord0;  // x: across [-1,1], y: metres along the flow
  gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
}
