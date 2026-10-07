#include "audio/soundscape.h"

#include <algorithm>
#include <cmath>

namespace aaa::audio {
namespace {
constexpr float kTau = 6.28318530718f;
inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline float onePole(float& s, float x, float a) { s += a * (x - s); return s; }
// Chamberlin state-variable filter, returns band-pass; `low` keeps the low-pass state.
inline float svfBand(float& band, float& low, float x, float f, float q) {
  low += f * band;
  const float high = x - low - q * band;
  band += f * high;
  return band;
}
inline float env(float t, float attack, float dur) {
  if (t < attack) return t / attack;
  const float r = (t - attack) / std::max(dur - attack, 1e-4f);
  return std::max(0.0f, 1.0f - r) * std::max(0.0f, 1.0f - r);
}
}  // namespace

Soundscape::Soundscape(int sampleRate) : sr_(sampleRate) { cur_ = target_; cur_.master = 0.0f; }

float Soundscape::noise() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return static_cast<float>(rng_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

int Soundscape::activeVoices() const {
  int n = 0;
  for (const Voice& v : voices_) n += v.active ? 1 : 0;
  return n;
}

void Soundscape::play(Sfx sfx, float gain, float pan, float distance) {
  Voice* slot = nullptr;
  for (Voice& v : voices_)
    if (!v.active) { slot = &v; break; }
  if (!slot) {  // steal the oldest
    slot = &voices_[0];
    for (Voice& v : voices_)
      if (v.t > slot->t) slot = &v;
  }
  Voice v;
  v.active = true;
  v.type = sfx;
  v.gain = clamp01(gain);
  pan = std::max(-1.0f, std::min(1.0f, pan));
  v.panL = std::cos((pan + 1.0f) * 0.25f * 3.14159265f);
  v.panR = std::sin((pan + 1.0f) * 0.25f * 3.14159265f);
  v.rng = (rng_ += 0x6d2b79f5u) | 1u;
  // Air absorption: farther sounds lose their highs.
  const float cutoff = 16000.0f / (1.0f + distance * 0.05f);
  v.air = 1.0f - std::exp(-kTau * cutoff / sr_);
  switch (sfx) {
    case Sfx::StepEarth: v.dur = 0.12f; v.p0 = 700.0f + 300.0f * clamp01(noise() * 0.5f + 0.5f); break;
    case Sfx::StepWater: v.dur = 0.28f; break;
    case Sfx::StepStone: v.dur = 0.07f; break;
    case Sfx::Pickup: v.dur = 0.35f; break;
    case Sfx::Eat: v.dur = 0.5f; break;
    case Sfx::Drink: v.dur = 0.7f; break;
    case Sfx::FireLit: v.dur = 1.4f; break;
    case Sfx::FireFed: v.dur = 0.6f; break;
    case Sfx::FireOut: v.dur = 1.5f; break;
    case Sfx::Howl: v.dur = 4.2f; v.p0 = 0.9f + 0.2f * (noise() * 0.5f + 0.5f); break;
    case Sfx::Growl: v.dur = 1.3f; break;
    case Sfx::Bite: v.dur = 0.5f; break;
    case Sfx::Bell: v.dur = 5.0f; break;
    case Sfx::Collapse: v.dur = 1.6f; break;
    case Sfx::Land: v.dur = 0.25f; break;
  }
  *slot = v;
}

float Soundscape::voiceSample(Voice& v, float dt) {
  const float t = v.t;
  float s = 0.0f;
  auto vn = [&]() {
    v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
    return static_cast<float>(v.rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
  };
  const float fs = static_cast<float>(sr_);
  auto f = [&](float hz) { return std::min(0.9f, 2.0f * std::sin(3.14159265f * hz / fs)); };
  switch (v.type) {
    case Sfx::StepEarth: {  // soft thud + leaf-litter crunch
      const float e = env(t, 0.004f, v.dur);
      v.phase += kTau * 70.0f * dt;
      s = std::sin(v.phase) * env(t, 0.002f, 0.06f) * 0.6f + onePole(v.lp, vn(), f(v.p0)) * e * 1.4f;
      break;
    }
    case Sfx::StepWater: {
      const float e = env(t, 0.01f, v.dur);
      s = svfBand(v.bp, v.bpLow, vn(), f(900.0f + 900.0f * t), 0.5f) * e * 1.2f;
      v.phase += kTau * (500.0f + 1200.0f * t) * dt;
      s += std::sin(v.phase) * env(t - 0.08f > 0 ? t - 0.08f : 0, 0.005f, 0.06f) * 0.15f;
      break;
    }
    case Sfx::StepStone: {
      s = svfBand(v.bp, v.bpLow, vn(), f(2400.0f), 0.3f) * env(t, 0.001f, v.dur) * 1.3f;
      break;
    }
    case Sfx::Pickup: {  // rustle: bursts of filtered noise
      const float grain = 0.5f + 0.5f * std::sin(t * 60.0f + std::sin(t * 23.0f) * 3.0f);
      s = svfBand(v.bp, v.bpLow, vn(), f(2200.0f), 0.7f) * env(t, 0.02f, v.dur) * grain * 1.2f;
      break;
    }
    case Sfx::Eat: {  // crunches
      const float c = std::fmod(t, 0.16f);
      s = svfBand(v.bp, v.bpLow, vn(), f(1500.0f), 0.6f) * env(c, 0.003f, 0.07f) * (t < 0.48f ? 1.0f : 0.0f);
      break;
    }
    case Sfx::Drink: {  // scoop splash + swallow gurgle
      s = onePole(v.lp, vn(), f(3000.0f * (1.0f - t))) * env(t, 0.01f, 0.35f) * 0.9f;
      v.phase += kTau * (180.0f + 90.0f * std::sin(t * 30.0f)) * dt;
      s += std::sin(v.phase) * env(t > 0.4f ? t - 0.4f : 0.0f, 0.02f, 0.25f) * (t > 0.4f ? 0.25f : 0.0f);
      break;
    }
    case Sfx::FireLit: {  // whoosh rising, then crackles
      const float sweep = 300.0f + 2500.0f * std::min(1.0f, t / 0.8f);
      s = onePole(v.lp, vn(), f(sweep)) * env(t, 0.25f, v.dur) * 0.9f;
      if (vn() > 0.995f) v.p1 = 1.0f;
      v.p1 *= 0.995f;
      s += vn() * v.p1 * 0.6f * (t > 0.3f ? 1.0f : 0.0f);
      break;
    }
    case Sfx::FireFed: {
      if (vn() > 0.993f) v.p1 = 1.0f;
      v.p1 *= 0.994f;
      s = vn() * v.p1 * 0.7f + onePole(v.lp, vn(), f(400.0f)) * env(t, 0.05f, v.dur) * 0.4f;
      break;
    }
    case Sfx::FireOut: {  // dying hiss
      s = svfBand(v.bp, v.bpLow, vn(), f(4000.0f), 0.8f) * env(t, 0.1f, v.dur) * 0.35f;
      break;
    }
    case Sfx::Howl: {  // glide up, hold, fall; harmonics shaped by a vowel-like formant
      const float x = t / v.dur;
      const float pitch = v.p0 * (380.0f + 260.0f * std::sin(std::min(1.0f, x * 1.6f) * 1.5708f) - 140.0f * std::max(0.0f, x - 0.65f) * 2.8f);
      const float vib = 1.0f + 0.012f * std::sin(t * 31.0f) * std::min(1.0f, t);
      v.phase += kTau * pitch * vib * dt;
      float tone = std::sin(v.phase) + 0.45f * std::sin(2.0f * v.phase) + 0.2f * std::sin(3.0f * v.phase);
      tone = svfBand(v.bp, v.bpLow, tone, f(900.0f), 0.9f) * 1.6f + tone * 0.15f;
      const float e = std::min(1.0f, t / 0.5f) * std::min(1.0f, (v.dur - t) / 0.9f);
      s = tone * std::max(0.0f, e) * 0.5f;
      break;
    }
    case Sfx::Growl: {
      v.phase += kTau * 32.0f * dt;
      const float am = 0.55f + 0.45f * std::sin(v.phase);
      s = onePole(v.lp, vn(), f(380.0f)) * am * env(t, 0.15f, v.dur) * 2.6f;
      break;
    }
    case Sfx::Bite: {
      s = svfBand(v.bp, v.bpLow, vn(), f(1800.0f), 0.4f) * env(t, 0.001f, 0.06f) * 1.6f;
      v.phase += kTau * 30.0f * dt;
      s += onePole(v.lp, vn(), f(300.0f)) * (0.6f + 0.4f * std::sin(v.phase)) * env(t, 0.03f, v.dur) * 2.0f;
      break;
    }
    case Sfx::Bell: {  // small bronze shrine bell: inharmonic partials, long decay
      const float partials[4] = {523.0f, 1288.0f, 2143.0f, 3020.0f};
      const float decays[4] = {1.6f, 0.9f, 0.55f, 0.3f};
      for (int i = 0; i < 4; ++i)
        s += std::sin(kTau * partials[i] * t) * std::exp(-t / decays[i]) * (0.5f / (1.0f + i));
      s *= std::min(1.0f, t / 0.003f);
      break;
    }
    case Sfx::Collapse: {
      v.phase += kTau * (60.0f - 25.0f * t) * dt;
      s = std::sin(v.phase) * env(t, 0.01f, 0.6f) * 0.8f + onePole(v.lp, vn(), f(250.0f)) * env(t, 0.02f, v.dur) * 1.2f;
      break;
    }
    case Sfx::Land: {
      v.phase += kTau * 55.0f * dt;
      s = std::sin(v.phase) * env(t, 0.003f, v.dur) * 0.9f + onePole(v.lp, vn(), f(600.0f)) * env(t, 0.002f, 0.1f);
      break;
    }
  }
  return onePole(v.airState, s, v.air) * v.gain;
}

void Soundscape::startBird() {
  bird_.active = true;
  bird_.t = 0.0f;
  bird_.notesLeft = 3 + static_cast<int>((noise() * 0.5f + 0.5f) * 5.0f);
  bird_.noteDur = 0.06f + 0.08f * (noise() * 0.5f + 0.5f);
  bird_.f0 = 2600.0f + 1800.0f * (noise() * 0.5f + 0.5f);
  bird_.f1 = bird_.f0 * (0.7f + 0.7f * (noise() * 0.5f + 0.5f));
  bird_.pan = noise() * 0.8f;
  bird_.gain = 0.04f + 0.05f * (noise() * 0.5f + 0.5f);
  bird_.gap = 0.03f + 0.06f * (noise() * 0.5f + 0.5f);
}

void Soundscape::render(float* out, int frames) {
  const float dt = 1.0f / static_cast<float>(sr_);
  const float smooth = 1.0f - std::exp(-dt / 0.35f);
  const float fs = static_cast<float>(sr_);
  auto fcut = [&](float hz) { return std::min(0.9f, 2.0f * std::sin(3.14159265f * hz / fs)); };
  for (int i = 0; i < frames; ++i) {
    // Smooth level changes to avoid zipper noise.
    cur_.wind += smooth * (target_.wind - cur_.wind);
    cur_.gust += smooth * 0.2f * (target_.gust - cur_.gust);
    cur_.stream += smooth * (target_.stream - cur_.stream);
    cur_.birds += smooth * (target_.birds - cur_.birds);
    cur_.insects += smooth * (target_.insects - cur_.insects);
    cur_.fire += smooth * (target_.fire - cur_.fire);
    cur_.threat += smooth * 0.3f * (target_.threat - cur_.threat);
    cur_.master += smooth * (target_.master - cur_.master);
    cur_.muffle += smooth * (target_.muffle - cur_.muffle);
    time_ += dt;
    const float tt = static_cast<float>(time_);
    float L = 0.0f, R = 0.0f;

    // Wind: two decorrelated band-limited noises, centre frequency breathing with gusts.
    if (cur_.wind > 0.001f) {
      const float g = cur_.wind * (0.5f + 0.7f * cur_.gust * (0.6f + 0.4f * std::sin(tt * 0.23f)));
      for (int c = 0; c < 2; ++c) {
        const float fc = 250.0f + 500.0f * cur_.gust + 140.0f * std::sin(tt * (0.31f + 0.07f * c) + c * 2.0f);
        const float n = onePole(windLp_[c], noise(), 0.08f);
        const float b = svfBand(windBp_[c], windBpL_[c], n, fcut(fc), 0.6f);
        (c == 0 ? L : R) += (b * 1.6f + windBpL_[c] * 0.35f) * g * 0.5f;
      }
    }
    // Stream: a broadband wash plus randomly pitched bubbles.
    if (cur_.stream > 0.001f) {
      const float wash = svfBand(streamBp_, streamBpL_, noise(), fcut(1100.0f), 1.2f) * 0.55f + onePole(streamLp_, noise(), 0.05f) * 0.6f;
      bubbleT_ -= dt;
      if (bubbleT_ <= 0.0f) {
        bubbleT_ = 0.01f + 0.06f * (noise() * 0.5f + 0.5f);
        bubbleF_ = 450.0f + 1300.0f * (noise() * 0.5f + 0.5f);
        bubbleAmp_ = 0.12f + 0.15f * (noise() * 0.5f + 0.5f);
        bubblePan_ = noise() * 0.7f;
      }
      bubbleF_ *= 1.0f + 2.5f * dt;  // bubbles chirp upward
      bubblePhase_ += kTau * bubbleF_ * dt;
      bubbleAmp_ *= 1.0f - 40.0f * dt;
      const float bub = std::sin(bubblePhase_) * bubbleAmp_;
      L += (wash + bub * (1.0f - bubblePan_)) * cur_.stream * 0.42f;
      R += (wash + bub * (1.0f + bubblePan_)) * cur_.stream * 0.42f;
    }
    // Birds: sparse phrases of swept chirps.
    if (cur_.birds > 0.02f) {
      birdTimer_ -= dt;
      if (!bird_.active && birdTimer_ <= 0.0f) {
        startBird();
        birdTimer_ = (1.5f + 6.0f * (noise() * 0.5f + 0.5f)) / (0.4f + cur_.birds);
      }
    }
    if (bird_.active) {
      bird_.t += dt;
      const float cycle = bird_.noteDur + bird_.gap;
      const float nt = std::fmod(bird_.t, cycle);
      if (bird_.t >= cycle * bird_.notesLeft) {
        bird_.active = false;
      } else if (nt < bird_.noteDur) {
        const float x = nt / bird_.noteDur;
        const float freq = bird_.f0 + (bird_.f1 - bird_.f0) * x + 300.0f * std::sin(x * 30.0f);
        bird_.phase += kTau * freq * dt;
        const float s = std::sin(bird_.phase) * std::sin(3.14159265f * x) * bird_.gain * cur_.birds;
        L += s * (1.0f - bird_.pan) * 0.7f;
        R += s * (1.0f + bird_.pan) * 0.7f;
      }
    }
    // Night insects: pulsed ~4.6 kHz chirps from two "crickets".
    if (cur_.insects > 0.01f) {
      cricketP_ += kTau * 4600.0f * dt;
      cricketT_ += dt;
      for (int c = 0; c < 2; ++c) {
        const float rate = c == 0 ? 2.6f : 3.3f;
        const float ph = std::fmod(cricketT_ * rate + c * 0.37f, 1.0f);
        const float burst = ph < 0.18f ? (0.5f + 0.5f * std::sin(cricketT_ * kTau * 32.0f)) : 0.0f;
        const float s = std::sin(cricketP_ * (c == 0 ? 1.0f : 1.07f)) * burst * 0.03f * cur_.insects;
        (c == 0 ? L : R) += s;
        (c == 0 ? R : L) += s * 0.3f;
      }
    }
    // Campfire: low roar + random crackles.
    if (cur_.fire > 0.001f) {
      const float roar = onePole(fireLp_, noise(), 0.02f) * 0.9f;
      if (noise() > 1.0f - 0.0009f * (0.5f + cur_.fire)) crackleAmp_ = 0.4f + 0.6f * (noise() * 0.5f + 0.5f);
      crackleAmp_ *= 1.0f - 300.0f * dt;
      const float n = noise();
      const float hp = n - firePrev_;
      firePrev_ = n;
      const float crackle = onePole(crackleLp_, hp, 0.5f) * crackleAmp_;
      L += (roar + crackle * 0.9f) * cur_.fire * 0.5f;
      R += (roar + crackle * 0.7f) * cur_.fire * 0.5f;
    }
    // Tension drone (felt more than heard).
    if (cur_.threat > 0.005f) {
      droneP1_ += kTau * 55.0f * dt;
      droneP2_ += kTau * 82.7f * dt;
      const float swell = 0.6f + 0.4f * std::sin(tt * 0.9f);
      const float d = (std::sin(droneP1_) + 0.6f * std::sin(droneP2_)) * cur_.threat * swell * 0.06f;
      L += d;
      R += d;
    }
    // One-shots.
    for (Voice& v : voices_) {
      if (!v.active) continue;
      const float s = voiceSample(v, dt);
      L += s * v.panL;
      R += s * v.panR;
      v.t += dt;
      if (v.t >= v.dur) v.active = false;
    }
    // Muffle (fade-outs), master gain and a soft clipper.
    const float mcut = 1.0f - std::exp(-kTau * (18000.0f * (1.0f - cur_.muffle) + 300.0f) / fs);
    L = onePole(muffleL_, L, mcut) * cur_.master;
    R = onePole(muffleR_, R, mcut) * cur_.master;
    out[2 * i] = std::tanh(L);
    out[2 * i + 1] = std::tanh(R);
  }
}

}  // namespace aaa::audio
