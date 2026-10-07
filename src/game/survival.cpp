#include "game/survival.h"

#include <cmath>

namespace aaa {

const char* damageCauseText(DamageCause c) {
  switch (c) {
    case DamageCause::Cold: return "The cold took you.";
    case DamageCause::Hunger: return "Hunger took you.";
    case DamageCause::Thirst: return "Thirst took you.";
    case DamageCause::Fall: return "You fell badly.";
    case DamageCause::Predator: return "The wolves brought you down.";
    case DamageCause::None: break;
  }
  return "";
}

float Survival::airTemperature(float hours, float altitude) {
  // Coldest just before dawn (~5h), warmest mid-afternoon (~15h). Mountain air: ~6 °C per km.
  const float phase = (hours - 15.0f) / 24.0f * kTwoPi;
  const float diurnal = 8.5f + 7.5f * std::cos(phase);  // 1..16 °C at the valley mouth
  return diurnal - altitude * 0.0065f;
}

void Survival::update(const SurvivalInputs& in) {
  if (collapsed_) return;
  const float dt = in.dt;
  const SurvivalTuning& T = tuning_;

  // Clothes soak quickly when wading and dry slowly (much faster by a fire).
  if (in.waterDepth > 0.25f) v_.wetness = std::fmin(1.0f, v_.wetness + dt * (0.15f + in.waterDepth * 0.4f));
  else v_.wetness = std::fmax(0.0f, v_.wetness - dt * (1.0f / 150.0f + in.fireHeat * (1.0f / 25.0f)));

  felt_ = in.airTemperature - v_.wetness * 7.0f + (in.sheltered ? 3.0f : 0.0f) + in.fireHeat * 22.0f;
  if (felt_ < T.comfortTemp) {
    v_.warmth -= (T.comfortTemp - felt_) * T.coolingPerDegree * dt;
  } else {
    v_.warmth += (T.warmingRate + in.fireHeat * T.fireWarming) * dt;
  }
  v_.warmth = clampf(v_.warmth, 0.0f, 100.0f);

  const float exert = in.sprinting ? T.exertionMultiplier : 1.0f;
  v_.satiety = clampf(v_.satiety - T.satietyDrain * exert * dt, 0.0f, 100.0f);
  v_.hydration = clampf(v_.hydration - T.hydrationDrain * exert * dt, 0.0f, 100.0f);

  if (in.landingSpeed > T.fallSafeSpeed) damage((in.landingSpeed - T.fallSafeSpeed) * T.fallDamagePerMs, DamageCause::Fall);

  // Needs at zero hurt; comfortable needs heal.
  if (v_.warmth <= 0.0f) damage(T.starvingDamage * 1.6f * dt, DamageCause::Cold);
  if (v_.hydration <= 0.0f) damage(T.starvingDamage * dt, DamageCause::Thirst);
  if (v_.satiety <= 0.0f) damage(T.starvingDamage * 0.6f * dt, DamageCause::Hunger);
  if (v_.warmth > 35.0f && v_.satiety > 25.0f && v_.hydration > 25.0f && !collapsed_)
    v_.health = std::fmin(100.0f, v_.health + T.regenRate * dt);
}

void Survival::damage(float amount, DamageCause cause) {
  if (collapsed_ || amount <= 0.0f) return;
  v_.health -= amount;
  lastCause_ = cause;
  if (v_.health <= 0.0f) {
    v_.health = 0.0f;
    collapsed_ = true;
  }
}

void Survival::eat(float nutrition, float water) {
  v_.satiety = clampf(v_.satiety + nutrition, 0.0f, 100.0f);
  v_.hydration = clampf(v_.hydration + water, 0.0f, 100.0f);
}

void Survival::drink(float amount) { v_.hydration = clampf(v_.hydration + amount, 0.0f, 100.0f); }

void Survival::reviveAfterCollapse() {
  collapsed_ = false;
  v_.health = 35.0f;
  v_.warmth = std::fmax(v_.warmth, 55.0f);
  v_.satiety = std::fmax(v_.satiety, 30.0f);
  v_.hydration = std::fmax(v_.hydration, 30.0f);
  v_.wetness = 0.0f;
  lastCause_ = DamageCause::None;
}

}  // namespace aaa
