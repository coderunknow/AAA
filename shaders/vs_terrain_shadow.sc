$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_heightmap, 0);
uniform vec4 u_terrainChunk;
uniform vec4 u_terrainInfo;

void main() {
  v_texcoord0 = vec2_splat(0.0);
  vec2 wxz = u_terrainChunk.xy + a_position.xz * u_terrainChunk.z;
  ivec2 texel = ivec2(clamp((wxz + u_terrainInfo.yy) / u_terrainInfo.w + 0.5, 0.0, u_terrainInfo.x - 1.0));
  float h = texelFetch(s_heightmap, texel, 0).x - a_position.y * u_terrainChunk.w;
  gl_Position = mul(u_viewProj, vec4(wxz.x, h, wxz.y, 1.0));
}
