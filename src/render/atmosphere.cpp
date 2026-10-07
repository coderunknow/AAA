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
  const Vec3 sun = trans * (5.2f * horizonFade);

  // Moonlight takes over at night (cool, dim), from the opposite side of the sky.
  const Vec3 moonDir = normalize(Vec3{-in.sunDir.x, std::fabs(in.sunDir.y) + 0.35f, -in.sunDir.z});
  const Vec3 moon = Vec3{0.32f, 0.42f, 0.6f} * 0.22f;
  const float night = 1.0f - smoothstep(-0.12f, 0.02f, sunY);
  s.lightDir = sunY > -0.04f ? in.sunDir : moonDir;
  if (sunY > -0.04f && sunY < 0.02f) s.lightDir = normalize(Vec3{in.sunDir.x, 0.02f, in.sunDir.z});
  s.sunColor = sunY > -0.04f ? sun : moon * night;

  // Sky colours.
  const float golden = (1.0f - smoothstep(0.05f, 0.35f, sunY)) * day;  // low sun warmth
  const Vec3 zenithDay{0.16f, 0.32f, 0.62f};
  const Vec3 horizonDay{0.62f, 0.70f, 0.78f};
  const Vec3 horizonGold{0.95f, 0.62f, 0.40f};
  const Vec3 zenithNight{0.006f, 0.010f, 0.022f};
  const Vec3 horizonNight{0.018f, 0.024f, 0.038f};
  s.skyZenith = lerp(zenithNight, zenithDay * (0.9f + 0.3f * day), day);
  s.skyHorizon = lerp(horizonNight, lerp(horizonDay, horizonGold, golden * 0.75f), day);
  s.skyAmbient = lerp(Vec3{0.02f, 0.028f, 0.05f}, (s.skyZenith * 0.55f + s.skyHorizon * 0.45f) * 0.85f, day);
  s.groundAmbient = lerp(Vec3{0.006f, 0.007f, 0.008f}, Vec3{0.075f, 0.07f, 0.045f} + sun * 0.012f, day);

  // Mist: thick in the early morning and evening, thinner at midday.
  const float morning = smoothstep(4.5f, 6.5f, in.hours) * (1.0f - smoothstep(8.5f, 11.5f, in.hours));
  const float evening = smoothstep(17.0f, 19.5f, in.hours);
  const float mistiness = 0.35f + 0.65f * std::fmax(morning, evening * 0.8f);
  s.fogColor = lerp(Vec3{0.02f, 0.026f, 0.04f}, s.skyHorizon * 0.92f + sun * 0.03f, day);
  s.fogDensity = 0.0011f + 0.0012f * mistiness;
  s.fogFalloff = 0.011f;
  s.mistBaseHeight = in.valleyFloor;
  s.valleyMist = 0.004f * mistiness;
  s.sunInscatter = 0.18f;
  s.sunDisc = 10.0f;
  s.exposure = lerp(3.2f, 0.95f, day);
  s.gradeHighlights = lerp(Vec3{0.98f, 1.0f, 1.05f}, Vec3{1.06f, 1.0f, 0.92f}, day);
  return s;
}

}  // namespace aaa
