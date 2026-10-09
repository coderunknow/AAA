#pragma once
// SDL3 audio device feeding the procedural Soundscape. On the web, SDL creates a WebAudio
// context and resumes it on the first user gesture (browser autoplay policy). Failure to open
// a device is not fatal: the game simply runs silently.
#include <memory>

#include "audio/soundscape.h"

struct SDL_AudioStream;

namespace aaa {

class AudioOutput {
 public:
  ~AudioOutput();
  bool init();
  void shutdown();
  bool active() const { return stream_ != nullptr; }
  // Thread-safe wrappers (the device callback may run on another thread natively).
  void setLevels(const audio::AmbienceLevels& l);
  void play(audio::Sfx sfx, float gain, float pan, float distance = 0.0f);
  // Master volume (persisted setting): scales ambience and one-shots.
  void setMasterVolume(float v) { master_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
  float masterVolume() const { return master_; }

 private:
  static void callback(void* user, SDL_AudioStream* stream, int additional, int total);
  SDL_AudioStream* stream_ = nullptr;
  std::unique_ptr<audio::Soundscape> synth_;
  float master_ = 1.0f;
};

}  // namespace aaa
