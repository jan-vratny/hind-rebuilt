#pragma once
#include <utility>
#include <vector>

#include <SDL.h>

// A small software mixer on SDL's audio device: voices playing the original's 11025 Hz samples
// once or in a loop, each with its volume, stereo position and pitch. The game thread starts,
// adjusts and stops them; changes of volume glide over one buffer so that they do not click.
namespace audio {

class Mixer {
public:
  ~Mixer() { close(); }
  // Opens the default output device; false (and silence) when there is none.
  bool open(float volume);
  void close();
  bool ready() const { return dev_ != 0; }

  // Plays `samples` (which must outlive the voice); returns its handle, 0 when not playing.
  // pan -1 left .. 1 right, pitch 1 = as recorded.
  int play(const std::vector<float>& samples, float gain, float pan = 0.0f, float pitch = 1.0f, bool loop = false);
  // Two voices starting together, one on the left and one on the right: a stereo recording.
  std::pair<int, int> playStereo(const std::vector<float>& left, const std::vector<float>& right, float gain, float pitch, bool loop);
  void set(int voice, float gain, float pan, float pitch);
  void stop(int voice);
  bool playing(int voice) const;
  // All but the voices kept playing.
  void stopAll();
  // The overall volume (0..1), at once.
  void setVolume(float volume);
  // Everything holds where it is (the game paused) and goes on from there; not the voices kept
  // playing (a briefing's speech over the paused game, over the flight's sounds set up anew).
  void pause(bool on);
  void keepPlaying(int voice);

private:
  struct Voice {
    int id = 0;
    const std::vector<float>* samples = nullptr;
    double pos = 0.0;
    float gain = 1.0f, pan = 0.0f, pitch = 1.0f;
    float left = 0.0f, right = 0.0f;  // the gains last applied, for the glide
    bool loop = false, started = false, kept = false;
  };
  static void callback(void* self, Uint8* stream, int len);
  void mix(float* out, int frames);

  SDL_AudioDeviceID dev_ = 0;
  int rate_ = 44100;
  float volume_ = 1.0f;
  bool paused_ = false;
  int nextId_ = 1;
  std::vector<Voice> voices_;
};

}  // namespace audio
