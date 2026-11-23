#pragma once
#include <deque>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "audio/mixer.hpp"
#include "gamedata/sound.hpp"
#include "sim/enemy.hpp"
#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "sim/threats.hpp"
#include "sim/weapons.hpp"
#include "world/terrain.hpp"

// What the game sounds like, from the original's sounds: the engines and blades, the weapons,
// the explosions near and far (heard late by the speed of sound), the touchdowns, and the voices
// on the intercom and the radio: Natasha's warnings, the WSO, ground control. The voices take
// turns on one channel.
namespace audio {

// The state of the flight the sounds follow, once a frame.
struct Scene {
  bool paused = false;
  glm::vec3 ear{0.0f}, earRight{1.0f, 0.0f, 0.0f};  // the listener (the camera)
  bool inside = true;                                // in the cockpit (else outside the helicopter)
  float gLoad = 1.0f;
  bool wso = true;            // the silicon WSO is on
  int wsoTarget = -1;         // the unit he has
  std::string wsoTargetObject;  // what it is (3DOBJS.BIN name)
  bool wsoTargetEnemy = true;
  int waypoint = -1;            // steered to, 0 home; -1 no mission
  float waypointBearing = 0.0f;   // deg, to the steer point
  float waypointDistance = 0.0f;  // m
  sim::Threat threat;
};

class GameAudio {
public:
  // Silent (and every call harmless) without the original's sounds or an output device.
  bool init(const std::string& gameDir, float volume, float voiceVolume);
  bool ready() const { return mixer_.ready(); }
  // The volumes as the preferences change them: all, and the voices against the rest.
  void setVolumes(float volume, float voiceVolume) {
    mixer_.setVolume(volume);
    voiceVolume_ = voiceVolume;
  }

  // A new flight from the start (a mission's or the exercise's): the old sounds stop, ground
  // control clears the take-off of a mission.
  // Over Ukraine the crew is not addressed as comrades (see reloaded_).
  void startFlight(bool mission, bool ukraine = false);
  // Leaving the flying (to the menus).
  void stop();

  void update(float dt, const Scene& scene, const sim::Helicopter& heli, const sim::Weapons& weapons,
              const sim::EnemyFire& enemy);
  // What happened this frame.
  void fired(sim::WeaponType w);
  void impact(const sim::Impact& i, const sim::Targets& targets, const world::Terrain& terrain);
  void enemyEvent(const sim::EnemyEvent& e, const sim::Helicopter& heli);
  // The drones in the air (sim::UnitClass::Drone): the buzz of their engines, the nearest few
  // heard by their distance, with the Doppler shift of their speed and the listener's (`earVel`).
  void drones(const sim::Targets& targets, glm::vec3 earVel);
  // A line on the radio (ground control's answer to the reconnaissance data, ...).
  void radio(int id) { say({id}); }
  // A voice at once, cutting off the one before (the menus' recognition library).
  void announce(int id) {
    if (!ready()) return;
    if (voice_) mixer_.stop(voice_);
    voice_ = mixer_.play(sample(id), voiceVolume_);
  }
  // A briefing's speech from the original's CD (gamedata::Cd: 8-bit at 22050 Hz), cutting off
  // the one before; stopSpeech when its page is left.
  void speak(const std::vector<uint8_t>& raw);
  void stopSpeech();
  bool speaking() const { return speechVoice_ && mixer_.playing(speechVoice_); }
  // The menus' music from the original's CD (SOUND/*.RAW: 8-bit stereo at 22050 Hz), cutting off
  // the one before; quieter under the speech (duck).
  void music(const std::vector<uint8_t>& raw, bool loop);
  void stopMusic();
  void duckMusic(bool duck);
  // A sound of the helicopter itself (the doors, the troops' feet).
  void effect(int id) { if (ready()) here(id, 0.6f); }
  // The lone bowman's warhead creaking a turn further onto its arrow (the remake's own sound).
  void squeak();

private:
  struct Line {
    std::vector<int> parts;  // blocks said one after another
    float wait = 0.0f;       // s before it starts
  };
  struct Delayed {
    int id;
    glm::vec3 pos;
    float gain, ref, at;  // ref: distance (m) at which it is heard at `gain`
  };
  void makeShahed();
  void makeJetShahed();
  void loadSqueak();
  void say(std::vector<int> parts, float wait = 0.0f);
  void sayOnce(int id, float& cooldown, float again);
  // A sound out there, heard when it arrives (`later` s after that).
  void at(int id, glm::vec3 pos, float gain, float ref, float later = 0.0f);
  void positioned(int id, glm::vec3 pos, float gain, float ref);
  // A sound of the helicopter itself: in the cockpit as it is, from outside by the distance.
  int here(int id, float gain, bool loop = false);
  void keep(int& voice, int id, bool on, float gain, float pitch = 1.0f);  // a loop of the helicopter, on or off
  float pan(glm::vec3 pos) const;

  // A block as played: some are cut short (see init).
  const std::vector<float>& sample(int id) const;

  gamedata::SoundBank bank_;
  std::vector<float> rocket2_;
  // "Comrade, weapons reloaded" without its "Comrade," for the Ukrainian missions.
  std::vector<float> reloaded_;
  bool ukraine_ = false;
  std::vector<float> shahed_, jetShahed_;  // made here: not in the original
  std::vector<float> squeak_;              // data/sounds/squeak.wav, at kSpeechRate
  // The speech played and the ones before (a stopped voice fades out over the next buffer).
  std::deque<std::vector<float>> speech_;
  int speechVoice_ = 0;
  // The music as speech_: left and right.
  std::deque<std::pair<std::vector<float>, std::vector<float>>> music_;
  std::pair<int, int> musicVoices_{0, 0};
  bool musicDucked_ = false;
  static constexpr float kMusicGain = 0.6f, kMusicDucked = 0.2f;
  struct DroneVoice {
    int unit, voice;
  };
  std::vector<DroneVoice> droneVoices_;
  Mixer mixer_;
  float voiceVolume_ = 1.0f;
  float time_ = 0.0f;
  Scene scene_;
  glm::vec3 heliPos_{0.0f};
  bool cleared_ = false;  // to land

  int reloads_ = 0;
  int rotor_ = 0, gun_ = 0, cannon_ = 0, doorGun_ = 0, turret_ = 0, voice_ = 0;
  float gunFired_ = -1.0f, cannonFired_ = -1.0f, doorFired_ = -1.0f, rocketSound_ = -1.0f;
  int rocketAlt_ = 0;
  std::deque<Line> lines_;
  size_t linePart_ = 0;
  float lineGap_ = 0.0f;
  std::vector<Delayed> delayed_;

  // What was so last time, to hear what changes.
  bool started_ = false;
  bool gearDown_ = true, onGround_ = true, crashed_ = false, engineOut_[2] = {}, engineFire_[2] = {}, fuelLow_ = false;
  float sinkRate_ = 0.0f;
  int flares_ = 0, chaff_ = 0, waypoint_ = -1, incoming_ = 0;
  bool armed_ = false, wso_ = true, autoFlares_ = false, autoChaff_ = false;
  int wsoTarget_ = -1;
  sim::WeaponType selected_ = sim::WeaponType::YakB;
  bool threat_ = false;
  int threatClass_ = 0;
  float overtorque_ = 0.0f, cdTorque_ = 0.0f, cdRotor_ = 0.0f, cdPullUp_ = 0.0f, cdThreat_ = 0.0f, cdGroundFire_ = 0.0f,
        cdFriendly_ = 0.0f, cdKill_ = 0.0f, cdCreak_ = 0.0f, cdHit_ = 0.0f;
  int friendly_ = 0, kill_ = 0;
};

}  // namespace audio
