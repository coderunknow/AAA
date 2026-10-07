#pragma once
// Procedural soundscape synthesiser: every sound in the game is generated here at runtime
// (no audio assets). Pure C++ with no platform dependencies so it can be unit-tested; the
// platform layer pulls interleaved stereo float samples from render().
//
// Beds (continuous, levels smoothed): wind, stream, birdsong (day), insects (night),
// campfire crackle, low tension drone. One-shots: footsteps, foraging, eating, drinking,
// fire ignition/feeding, wolf howls/growls/bites, the shrine bell, collapse, landing.
#include <array>
#include <cstdint>

namespace aaa::audio {

struct AmbienceLevels {
  float wind = 0.4f;     // 0..1
  float gust = 0.5f;     // 0..1, slow modulation from the renderer's wind
  float stream = 0.0f;   // 0..1, proximity to running water
  float birds = 0.0f;    // 0..1, daytime forest life
  float insects = 0.0f;  // 0..1, night chorus
  float fire = 0.0f;     // 0..1, proximity to a burning fire
  float threat = 0.0f;   // 0..1, predators nearby
  float master = 0.8f;   // overall volume
  float muffle = 0.0f;   // 0..1, low-pass everything (fade to black / collapse)
};

enum class Sfx : uint8_t {
  StepEarth, StepWater, StepStone, Pickup, Eat, Drink, FireLit, FireFed, FireOut, Howl, Growl, Bite, Bell, Collapse, Land
};

class Soundscape {
 public:
  explicit Soundscape(int sampleRate = 48000);
  void setLevels(const AmbienceLevels& l) { target_ = l; }
  // gain 0..1, pan -1 (left) .. 1 (right), distance in metres (adds air absorption).
  void play(Sfx sfx, float gain, float pan, float distance = 0.0f);
  void render(float* interleavedStereo, int frames);
  int sampleRate() const { return sr_; }
  int activeVoices() const;

 private:
  struct Voice {
    bool active = false;
    Sfx type = Sfx::StepEarth;
    float t = 0.0f, dur = 0.1f;
    float gain = 1.0f, panL = 0.7f, panR = 0.7f;
    float lp = 0.0f, lp2 = 0.0f, bp = 0.0f, bpLow = 0.0f;  // filter states
    float phase = 0.0f, phase2 = 0.0f;
    float air = 1.0f;  // one-pole coefficient for distance absorption
    float airState = 0.0f;
    uint32_t rng = 1;
    float p0 = 0.0f, p1 = 0.0f;  // per-voice parameters
  };
  float noise();
  float voiceSample(Voice& v, float dt);
  void startBird();

  int sr_;
  uint32_t rng_ = 0x9e3779b9u;
  AmbienceLevels target_, cur_;
  std::array<Voice, 16> voices_{};
  // Bed state.
  float windLp_[2] = {0, 0}, windBp_[2] = {0, 0}, windBpL_[2] = {0, 0};
  float streamBp_ = 0, streamBpL_ = 0, streamLp_ = 0;
  float bubbleT_ = 0, bubbleF_ = 600, bubblePhase_ = 0, bubbleAmp_ = 0, bubblePan_ = 0;
  float fireLp_ = 0, firePrev_ = 0, crackleAmp_ = 0, crackleLp_ = 0;
  float droneP1_ = 0, droneP2_ = 0;
  float cricketP_ = 0, cricketT_ = 0;
  float birdTimer_ = 3.0f;
  struct Bird {
    bool active = false;
    float t = 0, noteDur = 0.1f, f0 = 3000, f1 = 4000, phase = 0, pan = 0, gain = 0;
    int notesLeft = 0;
    float gap = 0;
  } bird_;
  float muffleL_ = 0, muffleR_ = 0;
  double time_ = 0.0;
};

}  // namespace aaa::audio
