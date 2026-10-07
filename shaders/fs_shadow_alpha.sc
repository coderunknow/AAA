$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_foliage, 0);

void main() {
  if (texture2D(s_foliage, v_texcoord0).a < 0.5) discard;
  gl_FragColor = vec4_splat(0.0);
}
