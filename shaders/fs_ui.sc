$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// SDF text + solid quads. The atlas is a single-channel (R8) signed-distance field;
// texel (0,0) is reserved pure white so solid rectangles reuse the same shader.
// Output is premultiplied (rgb * a, a) and blended with (ONE, INV_SRC_ALPHA), which
// also keeps the destination alpha at 1 so the WebGL canvas stays opaque.

SAMPLER2D(s_atlas, 0);

void main() {
  float d = texture2D(s_atlas, v_texcoord0).r;
  // Screen-space anti-aliased edge for the distance field.
  float w = fwidth(d);
  float a = smoothstep(0.5 - w, 0.5 + w, d) * v_color0.a;
  gl_FragColor = vec4(v_color0.rgb * a, a);
}
