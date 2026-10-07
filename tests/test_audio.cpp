#include <cmath>
#include <vector>

#include "audio/soundscape.h"
#include "test.h"

using namespace aaa::audio;

namespace {
double rms(const std::vector<float>& b) {
  double s = 0.0;
  for (float x : b) s += double(x) * x;
  return std::sqrt(s / std::max<size_t>(b.size(), 1));
}
}  // namespace

TEST_CASE("audio: beds and one-shots render finite, bounded, audible output") {
  Soundscape sc(48000);
  AmbienceLevels l;
  l.wind = 0.6f; l.stream = 0.8f; l.birds = 1.0f; l.fire = 0.7f; l.threat = 0.5f; l.master = 0.8f;
  sc.setLevels(l);
  const Sfx all[] = {Sfx::StepEarth, Sfx::StepWater, Sfx::StepStone, Sfx::Pickup, Sfx::Eat, Sfx::Drink, Sfx::FireLit,
                     Sfx::FireFed, Sfx::FireOut, Sfx::Howl, Sfx::Growl, Sfx::Bite, Sfx::Bell, Sfx::Collapse, Sfx::Land};
  for (Sfx s : all) sc.play(s, 0.8f, 0.3f, 10.0f);
  CHECK(sc.activeVoices() == 15);
  std::vector<float> buf(48000 * 2);
  sc.render(buf.data(), 48000);
  bool finite = true, bounded = true;
  for (float x : buf) { finite &= std::isfinite(x); bounded &= std::fabs(x) <= 1.0f; }
  CHECK(finite);
  CHECK(bounded);
  CHECK(rms(buf) > 0.01);
  // Sounds longer than a second (ignition, fire out, howl, growl, bell, collapse) remain.
  CHECK(sc.activeVoices() == 6);
  for (int i = 0; i < 6; ++i) sc.render(buf.data(), 48000);
  CHECK(sc.activeVoices() == 0);
}

TEST_CASE("audio: master 0 is silent and night insects differ from day birds") {
  Soundscape sc(48000);
  AmbienceLevels l;
  l.master = 0.0f;
  sc.setLevels(l);
  std::vector<float> buf(4800 * 2);
  for (int i = 0; i < 10; ++i) sc.render(buf.data(), 4800);
  CHECK(rms(buf) < 1e-4);
  AmbienceLevels night;
  night.wind = 0.0f; night.insects = 1.0f; night.master = 1.0f;
  sc.setLevels(night);
  std::vector<float> second(48000 * 2);
  sc.render(second.data(), 48000);  // a full second spans several chirps
  CHECK(rms(second) > 0.002);
}
