#pragma once
// Semi-realistic survival model: a handful of readable needs, no spreadsheets.
//   warmth   — falls when the felt temperature is low (night, altitude, wet clothes, wind),
//              recovers near fire or in mild weather.
//   satiety  — slowly falls; foraging restores it.
//   hydration— falls faster than satiety; drinking from the stream restores it.
//   health   — hurt by falls, bites and any need at zero; regenerates when needs are met.
// Pure logic (no rendering/platform), deterministic for a given input sequence.
#include "core/math.h"

namespace aaa {

struct Vitals {
  float health = 100.0f;
  float warmth = 100.0f;
  float satiety = 85.0f;
  float hydration = 80.0f;
  float wetness = 0.0f;  // 0 dry .. 1 soaked
};

struct SurvivalTuning {
  float comfortTemp = 8.0f;        // °C felt temperature where warmth is stable
  float coolingPerDegree = 0.05f;   // warmth/s per °C below comfort
  float warmingRate = 0.6f;         // warmth/s in comfortable conditions
  float fireWarming = 4.0f;         // extra warmth/s right next to a fire
  float satietyDrain = 0.055f;      // per second (~30 min from full)
  float hydrationDrain = 0.085f;    // per second (~20 min from full)
  float exertionMultiplier = 1.8f;  // drains while sprinting
  float starvingDamage = 0.35f;     // health/s per need at zero
  float regenRate = 0.22f;          // health/s when all needs are comfortable
  float fallSafeSpeed = 9.5f;       // landing speed (m/s) without injury
  float fallDamagePerMs = 13.0f;
};

struct SurvivalInputs {
  float dt = 0.0f;
  float airTemperature = 12.0f;  // °C, from time of day + altitude
  float fireHeat = 0.0f;         // 0..1 proximity to burning fire(s)
  float waterDepth = 0.0f;       // metres at the player's position
  bool sheltered = false;        // under the shrine roof
  bool sprinting = false;
  float landingSpeed = 0.0f;     // > 0 on the frame the player lands
};

enum class DamageCause { None, Cold, Hunger, Thirst, Fall, Predator };
const char* damageCauseText(DamageCause c);

class Survival {
 public:
  void update(const SurvivalInputs& in);
  void damage(float amount, DamageCause cause);
  void eat(float nutrition, float water);
  void drink(float amount);
  void restore(const Vitals& v) { v_ = v; collapsed_ = v_.health <= 0.0f; }
  // After collapsing: wake up weakened but alive.
  void reviveAfterCollapse();

  const Vitals& vitals() const { return v_; }
  bool collapsed() const { return collapsed_; }
  DamageCause lastDamage() const { return lastCause_; }
  float feltTemperature() const { return felt_; }
  const SurvivalTuning& tuning() const { return tuning_; }

  // Ambient air temperature (°C) for a time of day and altitude. Pure helper.
  static float airTemperature(float hours, float altitude);

 private:
  SurvivalTuning tuning_;
  Vitals v_;
  float felt_ = 12.0f;
  bool collapsed_ = false;
  DamageCause lastCause_ = DamageCause::None;
};

}  // namespace aaa
