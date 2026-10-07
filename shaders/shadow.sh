// Shadow receiving: two cascades packed side by side in one depth atlas.
SAMPLER2DSHADOW(s_shadowMap, 4);

float shadowCascade(vec3 _wpos, int _cascade) {
  vec4 sc = mul(u_shadowMtx[_cascade], vec4(_wpos, 1.0));
  vec3 uvz = sc.xyz / sc.w;
  float texel = u_shadowParams.x;
  float sum = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      sum += shadow2D(s_shadowMap, vec3(uvz.xy + vec2(float(x), float(y)) * texel, uvz.z));
  return sum / 9.0;
}

// Returns 1 = fully lit, 0 = fully shadowed. Blends between cascades near the split.
float sunShadow(vec3 _wpos, vec3 _n) {
  if (u_shadowParams.z < 0.5) return 1.0;
  float dist = length(_wpos - u_camPos.xyz);
  // Normal offset reduces acne on slopes facing away from the sun.
  vec3 p = _wpos + _n * (0.04 + 0.08 * (1.0 - saturate1(dot(_n, u_sunDir.xyz))));
  float split = u_shadowParams.y;
  float farRange = u_shadowParams.w;
  if (dist < split * 0.85) return shadowCascade(p, 0);
  if (dist < split) return mix(shadowCascade(p, 0), shadowCascade(p + _n * 0.25, 1), (dist - split * 0.85) / (split * 0.15));
  float s = shadowCascade(p + _n * 0.25, 1);
  return mix(s, 1.0, smoothstep(farRange * 0.8, farRange, dist));
}
