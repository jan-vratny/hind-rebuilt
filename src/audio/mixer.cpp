#include "audio/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gamedata/sound.hpp"

namespace audio {

namespace {
constexpr size_t kMaxVoices = 40;
}

bool Mixer::open(float volume) {
  close();
  volume_ = std::clamp(volume, 0.0f, 1.0f);
  if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "No sound: %s\n", SDL_GetError());
    return false;
  }
  SDL_AudioSpec want{}, have{};
  want.freq = 44100;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = &Mixer::callback;
  want.userdata = this;
  dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!dev_) {
    std::fprintf(stderr, "No sound: %s\n", SDL_GetError());
    return false;
  }
  rate_ = have.freq;
  voices_.reserve(kMaxVoices);
  SDL_PauseAudioDevice(dev_, 0);
  return true;
}

void Mixer::setVolume(float volume) {
  if (dev_) SDL_LockAudioDevice(dev_);
  volume_ = std::clamp(volume, 0.0f, 1.0f);
  if (dev_) SDL_UnlockAudioDevice(dev_);
}

void Mixer::close() {
  if (dev_) SDL_CloseAudioDevice(dev_);
  dev_ = 0;
  voices_.clear();
}

int Mixer::play(const std::vector<float>& samples, float gain, float pan, float pitch, bool loop) {
  if (!dev_ || samples.empty() || gain <= 0.0f) return 0;
  SDL_LockAudioDevice(dev_);
  if (voices_.size() >= kMaxVoices) {
    // The quietest one makes room (a loop only for another loop).
    auto q = std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
      return (a.loop ? 10.0f : 0.0f) + a.gain < (b.loop ? 10.0f : 0.0f) + b.gain;
    });
    if (q->gain > gain && !loop) {
      SDL_UnlockAudioDevice(dev_);
      return 0;
    }
    voices_.erase(q);
  }
  Voice v;
  v.id = nextId_++;
  v.samples = &samples;
  v.gain = gain, v.pan = pan, v.pitch = pitch, v.loop = loop;
  voices_.push_back(v);
  SDL_UnlockAudioDevice(dev_);
  return v.id;
}

std::pair<int, int> Mixer::playStereo(const std::vector<float>& left, const std::vector<float>& right, float gain, float pitch, bool loop) {
  if (!dev_ || left.empty() || right.empty() || gain <= 0.0f) return {0, 0};
  SDL_LockAudioDevice(dev_);
  int ids[2];
  for (int k = 0; k < 2; ++k) {
    Voice v;
    v.id = ids[k] = nextId_++;
    v.samples = k ? &right : &left;
    v.gain = gain, v.pan = k ? 1.0f : -1.0f, v.pitch = pitch, v.loop = loop;
    voices_.push_back(v);
  }
  SDL_UnlockAudioDevice(dev_);
  return {ids[0], ids[1]};
}

void Mixer::set(int voice, float gain, float pan, float pitch) {
  if (!dev_ || !voice) return;
  SDL_LockAudioDevice(dev_);
  for (Voice& v : voices_)
    if (v.id == voice) v.gain = std::max(gain, 0.0f), v.pan = pan, v.pitch = pitch;
  SDL_UnlockAudioDevice(dev_);
}

void Mixer::stop(int voice) {
  if (!dev_ || !voice) return;
  SDL_LockAudioDevice(dev_);
  // It fades out over the next buffer rather than stop dead.
  for (Voice& v : voices_)
    if (v.id == voice) v.gain = 0.0f, v.loop = false, v.id = -1;
  SDL_UnlockAudioDevice(dev_);
}

bool Mixer::playing(int voice) const {
  if (!dev_ || !voice) return false;
  SDL_LockAudioDevice(dev_);
  bool on = std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice; });
  SDL_UnlockAudioDevice(dev_);
  return on;
}

void Mixer::stopAll() {
  if (!dev_) return;
  SDL_LockAudioDevice(dev_);
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return !v.kept; }), voices_.end());
  SDL_UnlockAudioDevice(dev_);
}

void Mixer::pause(bool on) {
  if (!dev_ || on == paused_) return;
  SDL_LockAudioDevice(dev_);
  paused_ = on;
  SDL_UnlockAudioDevice(dev_);
}

void Mixer::keepPlaying(int voice) {
  if (!dev_ || !voice) return;
  SDL_LockAudioDevice(dev_);
  for (Voice& v : voices_)
    if (v.id == voice) v.kept = true;
  SDL_UnlockAudioDevice(dev_);
}

void Mixer::callback(void* self, Uint8* stream, int len) {
  static_cast<Mixer*>(self)->mix(reinterpret_cast<float*>(stream), len / int(2 * sizeof(float)));
}

void Mixer::mix(float* out, int frames) {
  std::fill(out, out + 2 * frames, 0.0f);
  const double step = double(gamedata::kSoundRate) / double(rate_);
  for (Voice& v : voices_) {
    if (paused_ && !v.kept) continue;
    const std::vector<float>& s = *v.samples;
    const size_t n = s.size();
    // Equal-power pan; the gains glide from the last buffer's to the new ones.
    const float a = (std::clamp(v.pan, -1.0f, 1.0f) + 1.0f) * 0.785398f;
    const float l1 = v.gain * volume_ * std::cos(a), r1 = v.gain * volume_ * std::sin(a);
    float l0 = v.started ? v.left : l1, r0 = v.started ? v.right : r1;
    v.started = true;
    const double inc = step * std::max(v.pitch, 0.05f);
    for (int i = 0; i < frames; ++i) {
      if (v.pos >= double(n)) {
        if (!v.loop) break;
        v.pos = std::fmod(v.pos, double(n));
      }
      const size_t k = size_t(v.pos);
      const float f = float(v.pos - double(k));
      const float x = s[k] + (s[k + 1 < n ? k + 1 : (v.loop ? 0 : k)] - s[k]) * f;
      const float t = float(i) / float(frames);
      out[2 * i] += x * (l0 + (l1 - l0) * t);
      out[2 * i + 1] += x * (r0 + (r1 - r0) * t);
      v.pos += inc;
    }
    v.left = l1, v.right = r1;
  }
  // Finished voices and the faded stopped ones go.
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                               [this](const Voice& v) {
                                 // A stopped one held by the pause has nothing left to fade.
                                 if (v.id < 0) return v.started || (paused_ && !v.kept);
                                 return !v.loop && v.pos >= double(v.samples->size());
                               }),
                voices_.end());
  // A soft limit instead of hard clipping when many sounds come together.
  for (int i = 0; i < 2 * frames; ++i) out[i] = std::tanh(out[i]);
}

}  // namespace audio
