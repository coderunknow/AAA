$input a_position
$output v_texcoord0, v_dir

#include <bgfx_shader.sh>

uniform mat4 u_invViewProjSky;  // inverse of (view * proj) without translation
uniform vec4 u_screenParams;    // x: 1 if render-target origin is bottom-left

void main() {
  gl_Position = vec4(a_position.xy, 1.0, 1.0);
  v_texcoord0 = vec2(a_position.x * 0.5 + 0.5, u_screenParams.x > 0.5 ? a_position.y * 0.5 + 0.5 : 0.5 - a_position.y * 0.5);
  vec4 far = mul(u_invViewProjSky, vec4(a_position.xy, 1.0, 1.0));
  v_dir = far.xyz / far.w;
}
