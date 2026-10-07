$input a_position, a_normal, a_texcoord0, a_color0, i_data0, i_data1
$output v_wpos, v_normal, v_texcoord0, v_color0, v_extra

#include <bgfx_shader.sh>
#include "common.sh"
#include "prop_vs.sh"

void main() {
  vec3 n;
  v_wpos = propWorldPos(a_position, i_data0, i_data1, a_color0, n, a_normal);
  v_normal = n;
  v_texcoord0 = a_texcoord0;
  v_color0 = a_color0;
  v_extra = vec4(i_data1.z, i_data0.w, a_position.y, 0.0);  // tint, scale, local height
  gl_Position = mul(u_viewProj, vec4(v_wpos, 1.0));
}
