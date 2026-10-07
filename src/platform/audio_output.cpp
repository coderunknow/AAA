#include "platform/audio_output.h"

#include <SDL3/SDL.h>

#include <vector>

#include "core/log.h"

namespace aaa {

AudioOutput::~AudioOutput() { shutdown(); }

bool AudioOutput::init() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    AAA_LOG_WARN("audio unavailable (%s); continuing without sound", SDL_GetError());
    return false;
  }
  const int rate = 48000;
  synth_ = std::make_unique<audio::Soundscape>(rate);
  SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioOutput::callback, this);
  if (!stream_) {
    AAA_LOG_WARN("could not open an audio device (%s); continuing without sound", SDL_GetError());
    synth_.reset();
    return false;
  }
  SDL_ResumeAudioStreamDevice(stream_);
  AAA_LOG_INFO("audio: procedural soundscape at %d Hz stereo", rate);
  return true;
}

void AudioOutput::shutdown() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  synth_.reset();
}

void AudioOutput::callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
  auto* self = static_cast<AudioOutput*>(user);
  if (!self->synth_ || additional <= 0) return;
  // SDL holds the stream lock while calling us, so the synth is safe to touch here.
  static thread_local std::vector<float> buf;
  const int frames = additional / static_cast<int>(2 * sizeof(float));
  if (frames <= 0) return;
  buf.resize(static_cast<size_t>(frames) * 2);
  self->synth_->render(buf.data(), frames);
  SDL_PutAudioStreamData(stream, buf.data(), frames * static_cast<int>(2 * sizeof(float)));
}

void AudioOutput::setLevels(const audio::AmbienceLevels& l) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  synth_->setLevels(l);
  SDL_UnlockAudioStream(stream_);
}

void AudioOutput::play(audio::Sfx sfx, float gain, float pan, float distance) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  synth_->play(sfx, gain, pan, distance);
  SDL_UnlockAudioStream(stream_);
}

}  // namespace aaa
