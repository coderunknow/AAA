$input v_texcoord0, v_dir

#include <bgfx_shader.sh>

// Generic separable 9-tap gaussian blur. Used for the bloom chain and the SSAO
// blur; the source texture is bound to s_blur, the direction to u_blurDir
// (direction * texel size, in uv units).
SAMPLER2D(s_blur, 0);
uniform vec4 u_blurDir;  // xy: step in uv units, zw: unused

void main() {
  vec2 uv = v_texcoord0;
  vec2 step = u_blurDir.xy;
  // 9-tap gaussian (sigma ~ 2.2 texels), weights sum to 1.
  const float w0 = 0.2270270270;
  const float w1 = 0.1945946;
  const float w2 = 0.1216216;
  const float w3 = 0.0540540;
  const float w4 = 0.0162160;
  vec3 sum = texture2D(s_blur, uv).rgb * w0;
  sum += texture2D(s_blur, uv - step * 1.0).rgb * w1;
  sum += texture2D(s_blur, uv + step * 1.0).rgb * w1;
  sum += texture2D(s_blur, uv - step * 2.0).rgb * w2;
  sum += texture2D(s_blur, uv + step * 2.0).rgb * w2;
  sum += texture2D(s_blur, uv - step * 3.0).rgb * w3;
  sum += texture2D(s_blur, uv + step * 3.0).rgb * w3;
  sum += texture2D(s_blur, uv - step * 4.0).rgb * w4;
  sum += texture2D(s_blur, uv + step * 4.0).rgb * w4;
  gl_FragColor = vec4(sum, 1.0);
}
