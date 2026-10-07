// Shared shader conventions.
//  * World space: Y up, left-handed, metres. All lighting is done in world space.
//  * Colours are linear; the scene renders to an HDR target, tonemapped in fs_tonemap.
//  * Per-frame uniforms below are set once per frame by the renderer (FrameUniforms).

uniform vec4 u_sunDir;        // xyz: direction toward the sun, w: daylight [0,1]
uniform vec4 u_sunColor;      // rgb: sun radiance (linear, pre-multiplied by intensity)
uniform vec4 u_skyAmbient;    // rgb: sky (upper hemisphere) ambient
uniform vec4 u_groundAmbient; // rgb: ground bounce ambient
uniform vec4 u_fogColor;      // rgb: fog / mist colour, w: global density
uniform vec4 u_fogParams;     // x: height falloff, y: mist base height, z: sun inscatter, w: valley mist density
uniform vec4 u_camPos;        // xyz: camera position, w: time (s)
uniform vec4 u_wind;          // xy: wind direction (xz), z: strength, w: gust
uniform vec4 u_shadowParams;  // x: texel size (atlas uv), y: cascade split distance, z: enabled, w: far cascade range
uniform mat4 u_shadowMtx[2];  // world -> shadow atlas (uv, depth) for each cascade
uniform vec4 u_fireLight;     // xyz: nearest campfire light position, w: radius (0 = off)
uniform vec4 u_fireColor;     // rgb: flickering fire radiance

#define PI 3.14159265

float saturate1(float x) { return clamp(x, 0.0, 1.0); }

// --- Lighting ----------------------------------------------------------------
vec3 hemiAmbient(vec3 _n) {
  return mix(u_groundAmbient.rgb, u_skyAmbient.rgb, _n.y * 0.5 + 0.5);
}

float ggxSpecular(vec3 _n, vec3 _v, vec3 _l, float _roughness) {
  vec3 h = normalize(_v + _l);
  float a = _roughness * _roughness;
  float a2 = a * a;
  float ndh = saturate1(dot(_n, h));
  float d = a2 / (PI * pow(ndh * ndh * (a2 - 1.0) + 1.0, 2.0));
  float ndl = saturate1(dot(_n, _l));
  float ndv = saturate1(dot(_n, _v)) + 1e-4;
  float k = a * 0.5;
  float vis = 1.0 / ((ndl * (1.0 - k) + k) * (ndv * (1.0 - k) + k));
  float f = 0.04 + 0.96 * pow(1.0 - saturate1(dot(_v, h)), 5.0);
  return d * vis * f * 0.25;
}

// Opaque dielectric surface (bark, rock, soil).
vec3 shadeSurface(vec3 _albedo, vec3 _n, vec3 _v, float _roughness, float _ao, float _shadow) {
  vec3 l = u_sunDir.xyz;
  float ndl = saturate1(dot(_n, l));
  vec3 direct = u_sunColor.rgb * _shadow * ndl * (_albedo + vec3_splat(ggxSpecular(_n, _v, l, _roughness)));
  // Ambient occlusion also dims bounce; a little sky shadowing in occluded areas.
  vec3 ambient = _albedo * hemiAmbient(_n) * _ao;
  return direct + ambient;
}

// Thin foliage: wrapped diffuse + back-lit translucency.
vec3 shadeFoliage(vec3 _albedo, vec3 _n, vec3 _v, float _ao, float _shadow, float _translucency) {
  vec3 l = u_sunDir.xyz;
  float wrap = saturate1((dot(_n, l) + 0.45) / 1.45);
  float back = pow(saturate1(dot(-_v, l)), 3.0) * _translucency;
  vec3 direct = u_sunColor.rgb * _shadow * (_albedo * wrap + _albedo * vec3(1.1, 1.25, 0.6) * back);
  vec3 ambient = _albedo * hemiAmbient(_n) * _ao;
  return direct + ambient;
}

// Nearest campfire as a soft, unshadowed point light (wrap lighting keeps it warm on everything).
vec3 localLight(vec3 _albedo, vec3 _n, vec3 _wpos) {
  vec3 d = u_fireLight.xyz - _wpos;
  float dist = length(d);
  vec3 l = d / max(dist, 1e-3);
  float att = (1.0 / (1.0 + dist * dist * 0.45)) * (1.0 - smoothstep(u_fireLight.w * 0.55, u_fireLight.w, dist));
  float wrap = saturate1((dot(_n, l) + 0.35) / 1.35);
  return _albedo * u_fireColor.rgb * (att * wrap);
}

// --- Atmosphere ----------------------------------------------------------------
// Height fog with analytic integration along the view ray plus a valley-mist layer.
vec3 applyFog(vec3 _color, vec3 _wpos) {
  vec3 ray = _wpos - u_camPos.xyz;
  float dist = length(ray);
  vec3 dir = ray / max(dist, 1e-3);
  float falloff = u_fogParams.x;
  // Global height fog (aerial perspective).
  float h0 = u_camPos.y - u_fogParams.y;
  float dy = dir.y * falloff * dist;
  float lineInt = abs(dy) > 1e-4 ? (1.0 - exp(-dy)) / dy : 1.0;
  float density = u_fogColor.w * exp(-falloff * h0) * dist * lineInt;
  // Dense low mist hugging the valley floor (separate, faster falloff).
  float mf = falloff * 6.0;
  float mdy = dir.y * mf * dist;
  float mLine = abs(mdy) > 1e-4 ? (1.0 - exp(-mdy)) / mdy : 1.0;
  density += u_fogParams.w * exp(-mf * h0) * dist * mLine;
  float fog = 1.0 - exp(-density);
  float sunAmt = pow(saturate1(dot(dir, u_sunDir.xyz)), 6.0);
  vec3 fogCol = u_fogColor.rgb + u_sunColor.rgb * sunAmt * u_fogParams.z;
  return mix(_color, fogCol, fog);
}

// --- Wind ------------------------------------------------------------------------
// _weight: 0 at the root, 1 at the tips (vertex colour g). _phase: per-branch phase.
vec3 windOffset(vec3 _wpos, float _weight, float _phase, float _flutter) {
  float t = u_camPos.w;
  vec2 dir = u_wind.xy;
  float strength = u_wind.z;
  float sway = sin(t * 0.9 + dot(_wpos.xz, vec2(0.031, 0.027)) + _phase * 2.0) * 0.6 +
               sin(t * 2.1 + _wpos.x * 0.07 + _phase * 6.0) * 0.25;
  float gust = 0.6 + 0.4 * sin(t * 0.35 + _wpos.z * 0.013) * u_wind.w;
  vec3 offs = vec3(dir.x, 0.0, dir.y) * (sway * strength * gust * _weight * _weight);
  // High-frequency leaf flutter.
  offs += vec3(sin(t * 7.0 + _phase * 31.0), sin(t * 9.0 + _phase * 17.0) * 0.5, cos(t * 8.0 + _phase * 23.0)) *
          (0.025 * _flutter * _weight * strength);
  return offs;
}

vec3 tonemapACES(vec3 _x) {
  // Narkowicz 2015 fit.
  const float a = 2.51; const float b = 0.03; const float c = 2.43; const float d = 0.59; const float e = 0.14;
  return clamp((_x * (a * _x + b)) / (_x * (c * _x + d) + e), 0.0, 1.0);
}
