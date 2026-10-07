$input a_position
$output v_wpos, v_texcoord0

#include <bgfx_shader.sh>
#include "common.sh"

SAMPLER2D(s_heightmap, 0);
uniform vec4 u_terrainChunk;  // xy: chunk origin (world xz), z: metres per grid step, w: skirt depth
uniform vec4 u_terrainInfo;   // x: heightmap resolution, y: world half size, z: 1 / world size, w: heightmap cell (m)

void main() {
  vec2 wxz = u_terrainChunk.xy + a_position.xz * u_terrainChunk.z;
  ivec2 texel = ivec2(clamp((wxz + u_terrainInfo.yy) / u_terrainInfo.w + 0.5, 0.0, u_terrainInfo.x - 1.0));
  float h = texelFetch(s_heightmap, texel, 0).x - a_position.y * u_terrainChunk.w;
  v_wpos = vec3(wxz.x, h, wxz.y);
  v_texcoord0 = (wxz + u_terrainInfo.yy) * u_terrainInfo.z;
  gl_Position = mul(u_viewProj, vec4(v_wpos, 1.0));
}
