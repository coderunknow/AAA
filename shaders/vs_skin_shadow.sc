$input a_position, a_normal, a_color0, a_color1, a_indices, a_weight
$output v_texcoord0

#include <bgfx_shader.sh>
#include "common.sh"
#include "skin.sh"

void main() {
  vec4 jointIds = a_indices * 255.0;
  vec4 weights = a_weight * (1.0 / 255.0);
  vec4 pos = skinPosition(vec4(a_position, 1.0), jointIds, weights);
  // One shadow fragment shader serves every caster, so the interface stays v_texcoord0.
  v_texcoord0 = vec2_splat(0.0);
  gl_Position = mul(u_viewProj, pos);
}
