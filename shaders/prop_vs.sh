// Shared instanced-prop vertex transform.
//   i_data0 = (position.xyz, uniform scale)
//   i_data1 = (sin yaw, cos yaw, tint, phase)
//   a_color0 = (ao, wind weight, foliage flag, branch phase)
vec3 rotateYaw(vec3 _v, float _s, float _c) { return vec3(_c * _v.x + _s * _v.z, _v.y, -_s * _v.x + _c * _v.z); }

vec3 propWorldPos(vec3 _local, vec4 _d0, vec4 _d1, vec4 _color, out vec3 _wnormal, vec3 _lnormal) {
  vec3 p = rotateYaw(_local * _d0.w, _d1.x, _d1.y);
  _wnormal = rotateYaw(_lnormal, _d1.x, _d1.y);
  vec3 wpos = _d0.xyz + p;
  // Taller props sway more in absolute terms: scale wind by instance scale.
  wpos += windOffset(wpos, _color.g, _color.a + _d1.w * 7.0, _color.b) * _d0.w;
  return wpos;
}
