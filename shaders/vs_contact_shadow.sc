$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

// Soft contact-shadow disc (PROMPT M4.2). a_position is a unit disc in the XZ plane;
// u_contactDisc places and scales it at a character's feet.
uniform vec4 u_contactDisc;    // xyz: disc centre (world), w: radius (m)
uniform vec4 u_contactParams;  // x: strength, y: lift above the ground (m)

void main() {
  float r = u_contactDisc.w;
  vec3 wpos = u_contactDisc.xyz + vec3(a_position.x * r, u_contactParams.y, a_position.z * r);
  v_texcoord0 = a_position.xz;  // unit disc coords: the fragment shader fades radially
  gl_Position = mul(u_viewProj, vec4(wpos, 1.0));
}
