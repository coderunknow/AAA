#include "render/atmosphere.h"

#include <cmath>

namespace aaa {

AtmosphereState evaluateAtmosphere(const AtmosphereInputs& in) {
  AtmosphereState s{};
  const float sunY = in.sunDir.y;
  const float elev = std::asin(clampf(sunY, -1.0f, 1.0f));
  const float day = smoothstep(-0.06f, 0.12f, sunY);
  s.daylight = day;

  // Transmittance through the atmosphere (Kasten-Young air mass, wavelength-dependent optical depth).
  const float elevDeg = std::fmax(elev * 180.0f / kPi, -2.0f);
  const float airMass = 1.0f / (std::sin(std::fmax(elev, 0.0f)) + 0.50572f * std::pow(elevDeg + 6.07995f, -1.6364f));
  const Vec3 tau{0.085f, 0.19f, 0.42f};
  const Vec3 trans{std::exp(-tau.x * airMass), std::exp(-tau.y * airMass), std::exp(-tau.z * airMass)};
  const float horizonFade = smoothstep(-0.03f, 0.06f, sunY);
  const Vec3 sun = trans * (3.1f * horizonFade);  // tuned with exposure for a soft, hazy key light

  // Moonlight takes over at night (cool, dim), from the opposite side of the sky.
  const Vec3 moonDir = normalize(Vec3{-in.sunDir.x, std::fabs(in.sunDir.y) + 0.35f, -in.sunDir.z});
  const Vec3 moon = Vec3{0.32f, 0.42f, 0.6f} * 0.22f;
  const float night = 1.0f - smoothstep(-0.12f, 0.02f, sunY);
  s.lightDir = sunY > -0.04f ? in.sunDir : moonDir;
  if (sunY > -0.04f && sunY < 0.02f) s.lightDir = normalize(Vec3{in.sunDir.x, 0.02f, in.sunDir.z});
  s.sunColor = sunY > -0.04f ? sun : moon * night;

  // Sky colours.
  const float golden = (1.0f - smoothstep(0.05f, 0.35f, sunY)) * day;  // low sun warmth
  const Vec3 zenithDay{0.20f, 0.32f, 0.52f};
  const Vec3 horizonDay{0.56f, 0.62f, 0.66f};  // humid, desaturated haze
  const Vec3 horizonGold{0.95f, 0.62f, 0.40f};
  const Vec3 zenithNight{0.006f, 0.010f, 0.022f};
  const Vec3 horizonNight{0.018f, 0.024f, 0.038f};
  s.skyZenith = lerp(zenithNight, zenithDay * (0.9f + 0.3f * day), day);
  s.skyHorizon = lerp(horizonNight, lerp(horizonDay, horizonGold, golden * 0.75f), day);
  s.skyAmbient = lerp(Vec3{0.02f, 0.028f, 0.05f}, (s.skyZenith * 0.5f + s.skyHorizon * 0.5f) * 0.72f, day);
  s.groundAmbient = lerp(Vec3{0.006f, 0.007f, 0.008f}, Vec3{0.075f, 0.07f, 0.045f} + sun * 0.012f, day);

  // Mist: thick in the early morning and evening, thinner at midday.
  const float morning = smoothstep(4.5f, 6.5f, in.hours) * (1.0f - smoothstep(8.5f, 11.5f, in.hours));
  const float evening = smoothstep(17.0f, 19.5f, in.hours);
  const float mistiness = 0.35f + 0.65f * std::fmax(morning, evening * 0.8f);
  const Vec3 mistGrey{0.50f, 0.55f, 0.58f};
  s.fogColor = lerp(Vec3{0.02f, 0.026f, 0.04f}, lerp(s.skyHorizon, mistGrey, 0.35f) * 0.95f + sun * 0.025f, day);
  s.fogDensity = 0.0026f + 0.0034f * mistiness;
  s.fogFalloff = 0.0075f;
  s.mistBaseHeight = in.valleyFloor;
  s.valleyMist = 0.018f * mistiness;
  s.sunInscatter = 0.18f;
  s.sunDisc = 10.0f;
  s.exposure = lerp(3.2f, 0.9f, day);
  s.gradeHighlights = lerp(Vec3{0.98f, 1.0f, 1.05f}, Vec3{1.06f, 1.0f, 0.92f}, day);

  // --- time-of-day grading presets (PROMPT M4.1) ----------------------------------------------
  // Five art-directed grades, blended smoothly by hour. Midday/morning keep the tuned
  // neutral baseline (saturation 1.06, contrast 1.06); dawn/dusk warm up, night cools and
  // desaturates. The grade is applied post-tonemap in fs_tonemap.
  struct GradePreset {
    Vec3 tint;
    float saturation, contrast;
  };
  const GradePreset kGrades[5] = {
      {Vec3{1.05f, 0.97f, 0.90f}, 1.10f, 1.04f},  // dawn: rose-gold
      {Vec3{1.01f, 1.00f, 0.99f}, 1.06f, 1.06f},  // morning: neutral-crisp (baseline)
      {Vec3{1.00f, 1.00f, 1.00f}, 1.04f, 1.08f},  // midday: neutral, slightly punchy
      {Vec3{1.06f, 0.96f, 0.88f}, 1.12f, 1.05f},  // dusk: warm amber
      {Vec3{0.94f, 0.98f, 1.06f}, 0.85f, 1.02f},  // night: cool, desaturated
  };
  const float h = std::fmod(in.hours + 24.0f, 24.0f);
  const float wDawn = smoothstep(4.5f, 6.0f, h) * (1.0f - smoothstep(7.5f, 9.0f, h));
  const float wMorning = smoothstep(7.5f, 9.0f, h) * (1.0f - smoothstep(11.5f, 13.0f, h));
  const float wMidday = smoothstep(11.5f, 13.0f, h) * (1.0f - smoothstep(15.5f, 17.0f, h));
  const float wDusk = smoothstep(15.5f, 17.0f, h) * (1.0f - smoothstep(19.0f, 20.5f, h));
  const float wNight = 1.0f - (wDawn + wMorning + wMidday + wDusk);  // fills the remaining hours
  Vec3 tint{0.0f, 0.0f, 0.0f};
  float saturation = 0.0f, contrast = 0.0f;
  auto accum = [&](float w, const GradePreset& p) {
    tint = tint + p.tint * w;
    saturation += p.saturation * w;
    contrast += p.contrast * w;
  };
  accum(wDawn, kGrades[0]);
  accum(wMorning, kGrades[1]);
  accum(wMidday, kGrades[2]);
  accum(wDusk, kGrades[3]);
  accum(wNight, kGrades[4]);
  s.gradeTint = tint;
  s.gradeSaturation = saturation;
  s.gradeContrast = contrast;
  return s;
}

}  // namespace aaa
