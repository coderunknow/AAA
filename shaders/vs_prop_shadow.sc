$input a_position, a_normal, a_texcoord0, a_color0, i_data0, i_data1
$output v_texcoord0

#include <bgfx_shader.sh>
#include "common.sh"
#include "prop_vs.sh"

void main() {
  vec3 n;
  vec3 wpos = propWorldPos(a_position, i_data0, i_data1, a_color0, n, a_normal);
  v_texcoord0 = a_texcoord0;
  gl_Position = mul(u_viewProj, vec4(wpos, 1.0));
}
