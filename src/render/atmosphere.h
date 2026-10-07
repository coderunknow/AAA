#pragma once
// Time-of-day driven lighting & atmosphere parameters (art-directed, physically motivated).
#include "core/math.h"

namespace aaa {

struct AtmosphereState {
  Vec3 lightDir;        // toward the dominant light (sun, or moon at night)
  Vec3 sunColor;        // HDR radiance of the dominant light
  Vec3 skyAmbient, groundAmbient;
  Vec3 skyZenith, skyHorizon;
  Vec3 fogColor;
  float fogDensity;     // per metre at the mist base height
  float fogFalloff;     // per metre of altitude
  float mistBaseHeight; // valley floor altitude
  float valleyMist;     // additional dense low mist
  float sunInscatter;
  float sunDisc;
  float exposure;
  float daylight;
  Vec3 gradeHighlights;
};

struct AtmosphereInputs {
  Vec3 sunDir;          // geometric sun direction (may be below the horizon)
  float hours;
  float valleyFloor;    // altitude of the valley floor (m)
};

AtmosphereState evaluateAtmosphere(const AtmosphereInputs& in);

}  // namespace aaa
