$input a_position, a_normal, a_texcoord0, a_color0, a_color1, a_indices, a_weight
$output v_wpos, v_normal, v_texcoord0, v_color0, v_color1

#include <bgfx_shader.sh>
#include "common.sh"
#include "skin.sh"

void main() {
  // a_indices / a_weight arrive as unorm8 4-vectors (COLOR2 / COLOR3 below).
  vec4 jointIds = a_indices * 255.0;
  vec4 weights = a_weight * (1.0 / 255.0);
  vec4 pos = skinPosition(vec4(a_position, 1.0), jointIds, weights);
  vec3 nrm = normalize(skinNormal(a_normal, jointIds, weights));
  v_wpos = pos.xyz;
  v_normal = nrm;
  v_texcoord0 = a_texcoord0;
  v_color0 = a_color0;   // albedo (rgb) + roughness (a)
  v_color1 = a_color1;   // weave frequency, weave strength, emissive, unused
  gl_Position = mul(u_viewProj, pos);
}
