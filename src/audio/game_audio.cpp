#include "audio/game_audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gamedata/cd.hpp"

namespace audio {

namespace snd = gamedata::snd;

namespace {

constexpr float kSpeedOfSound = 340.0f;  // m/s

// The WSO's words for the weapon he arms.
int armedLine(sim::WeaponType w) {
  switch (w) {
    case sim::WeaponType::YakB: return snd::ArmedGun;
    case sim::WeaponType::Upk23: return snd::ArmedCannon;
    case sim::WeaponType::S8: return snd::Armed80;
    case sim::WeaponType::S5: return snd::Armed57;
    case sim::WeaponType::Fab250:
    case sim::WeaponType::Ofab250: return snd::ArmedFab250;
    case sim::WeaponType::Fab500: return snd::ArmedFab500;
    case sim::WeaponType::Zab500: return snd::ArmedZab500;
    case sim::WeaponType::Shturm: return snd::ArmedAt6;
    case sim::WeaponType::Mine: return snd::ArmedMines;
    case sim::WeaponType::R60: return snd::ArmedR60;
    case sim::WeaponType::DoorGun:
    case sim::WeaponType::Interceptor: break;  // the crew's, never armed by the pilot
  }
  return snd::ArmedGun;
}

// Natasha's words for a light of the threat warning receiver.
int threatLine(int cls) {
  switch (sim::ThreatClass(cls)) {
    case sim::ThreatClass::EarlyWarning: return snd::RadarWarningFirst;
    case sim::ThreatClass::AircraftRadar: return snd::RadarWarningFirst + 1;
    case sim::ThreatClass::SamRadar: return snd::RadarWarningFirst + 2;
    case sim::ThreatClass::AaaRadar: return snd::RadarWarningFirst + 3;
    case sim::ThreatClass::RfMissile: return snd::IncomingRf;
    case sim::ThreatClass::IrMissile: return snd::IncomingIr;
  }
  return snd::RadarWarningFirst;
}

}  // namespace

const std::vector<float>& GameAudio::sample(int id) const {
  if (id == snd::Rocket2) return rocket2_;
  if (id == snd::Reloaded && ukraine_ && !reloaded_.empty()) return reloaded_;
  return bank_.get(id);
}

void GameAudio::makeShahed() {
  // The Shahed-136's two-stroke twin and pusher propeller, as heard over Ukraine: a loop of
  // about a second (whole periods, so it loops clean). Some 230 exhaust pulses a second (about
  // 7000 rpm), each a low rough blat with a crack on top, every one a little stronger or weaker,
  // earlier or later than the last, now and then all but missing (the two-stroke's uneven
  // firing); under them the hum of the revolutions, the rasp of the blades and a little of the
  // air they tear. Lightly overdriven for an edge.
  const float rate = float(gamedata::kSoundRate), pi2 = 2.0f * 3.14159265f;
  const int period = 48, pulses = 230, n = period * pulses;
  shahed_.assign(size_t(n), 0.0f);
  unsigned seed = 136u;
  auto uniform = [&](float lo, float hi) {
    seed = seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * float(seed >> 8) / float(1u << 24);
  };
  for (int k = 0; k < pulses; ++k) {
    float a = (k % 2 ? 0.75f : 1.0f) * uniform(0.65f, 1.2f);
    if (uniform(0.0f, 1.0f) < 0.08f) a *= 0.35f;
    const int shift = int(uniform(-4.0f, 5.0f));
    for (int j = 0; j < period + 40; ++j) {
      const float t = float(j) / rate;
      const float blat = std::exp(-t * 160.0f) * (0.9f * std::sin(pi2 * 260.0f * t) + 0.5f * std::sin(pi2 * 560.0f * t + 0.5f)) *
                         (1.0f + 0.6f * uniform(-1.0f, 1.0f));
      const float crack = std::exp(-t * 600.0f) * 0.3f * std::sin(pi2 * 2400.0f * t + 2.0f);
      shahed_[size_t(((k * period + j + shift) % n + n) % n)] += a * (blat + crack) * (j < period ? 0.9f : 0.5f);
    }
  }
  float prev = 0.0f;
  for (int i = 0; i < n; ++i) {
    const float noise = uniform(-1.0f, 1.0f), hiss = noise - prev;
    prev = noise;
    const float phase = std::fmod(float(i) * float(pulses) / float(n), 1.0f);
    shahed_[size_t(i)] += 0.35f * std::sin(pi2 * float(pulses / 2) * float(i) / float(n)) + 0.15f * (2.0f * phase - 1.0f) +
                          0.1f * hiss * (1.0f - phase) * (1.0f - phase);
  }
  float peak = 0.0f;
  for (float& v : shahed_) peak = std::max(peak, std::fabs(v = std::tanh(1.8f * v)));
  for (float& v : shahed_) v *= 0.9f / peak;
}

void GameAudio::makeJetShahed() {
  // The Geran-3's small turbojet (a Telefly JT80): the roar of its exhaust, a rushing noise low
  // and through the middle with a hiss on top, and over it the compressor's whine, slowly
  // wavering, with its shaft's hum an octave below. A loop of about a second, its ends faded
  // into each other.
  const float rate = float(gamedata::kSoundRate), pi2 = 2.0f * 3.14159265f;
  const int n = gamedata::kSoundRate, fade = n / 8;
  std::vector<float> s(size_t(n + fade), 0.0f);
  unsigned seed = 238u;
  auto uniform = [&](float lo, float hi) {
    seed = seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * float(seed >> 8) / float(1u << 24);
  };
  float low = 0.0f, mid = 0.0f, prev = 0.0f;
  for (size_t i = 0; i < s.size(); ++i) {
    const float t = float(i) / rate, w = uniform(-1.0f, 1.0f);
    low += 0.08f * (w - low);   // the deep rush
    mid += 0.35f * (w - mid);   // the body of the roar
    const float hiss = w - prev;
    prev = w;
    const float wobble = 1.0f + 0.004f * std::sin(pi2 * 3.0f * t) + 0.002f * std::sin(pi2 * 7.0f * t + 1.0f);
    const float whine = 0.22f * std::sin(pi2 * 3150.0f * wobble * t) + 0.1f * std::sin(pi2 * 1575.0f * wobble * t + 0.7f) +
                        0.06f * std::sin(pi2 * 4725.0f * wobble * t + 2.0f);
    s[i] = 1.6f * low + 0.7f * mid + 0.12f * hiss + whine;
  }
  for (int i = 0; i < fade; ++i) {
    const float a = float(i) / float(fade);
    s[size_t(i)] = s[size_t(i)] * a + s[size_t(n + i)] * (1.0f - a);
  }
  s.resize(size_t(n));
  float peak = 0.0f;
  for (float& v : s) peak = std::max(peak, std::fabs(v = std::tanh(1.4f * v)));
  for (float& v : s) v *= 0.9f / peak;
  jetShahed_ = std::move(s);
}

void GameAudio::drones(const sim::Targets& targets, glm::vec3 earVel) {
  if (!ready() || scene_.paused) return;
  struct Heard {
    int unit;
    float gain, pan, pitch;
  };
  std::vector<Heard> heard;
  for (size_t k = 0; k < targets.list.size(); ++k) {
    const sim::Vehicle& v = targets.list[k];
    if (sim::vehicleType(v.kind).cls != sim::UnitClass::Drone || !v.moving() || v.delay > 0.0f) continue;
    const float d = std::max(glm::distance(v.pos, scene_.ear), 1.0f);
    // Loud: heard a couple of kilometres off outside, less in the cockpit under the rotor; the
    // jet one louder still.
    const float g = (sim::isJetDrone(v.kind) ? 0.8f : 0.6f) * std::min(1.0f, 60.0f / d) * (scene_.inside ? 0.6f : 1.0f);
    if (g < 0.012f) continue;
    const glm::vec3 to = (scene_.ear - v.pos) / d;  // from the drone to the ear
    const float closing = glm::dot(v.vel, to), approach = glm::dot(earVel, -to);
    // A little different engine speed for each, so that two are not heard as one.
    const float own = 1.0f + 0.025f * float(int(k * 37 % 7) - 3) / 3.0f;
    const float pitch = std::clamp((kSpeedOfSound + approach) / std::max(kSpeedOfSound - closing, 50.0f), 0.6f, 1.6f) * own;
    heard.push_back({int(k), g, pan(v.pos), pitch});
  }
  // The three loudest.
  std::sort(heard.begin(), heard.end(), [](const Heard& a, const Heard& b) { return a.gain > b.gain; });
  if (heard.size() > 3) heard.resize(3);
  for (auto it = droneVoices_.begin(); it != droneVoices_.end();) {
    const bool still = std::any_of(heard.begin(), heard.end(), [&](const Heard& h) { return h.unit == it->unit; });
    if (still && mixer_.playing(it->voice)) {
      ++it;
      continue;
    }
    mixer_.stop(it->voice);
    it = droneVoices_.erase(it);
  }
  for (const Heard& h : heard) {
    auto it = std::find_if(droneVoices_.begin(), droneVoices_.end(), [&](const DroneVoice& dv) { return dv.unit == h.unit; });
    if (it != droneVoices_.end()) mixer_.set(it->voice, h.gain, h.pan, h.pitch);
    else if (const int voice = mixer_.play(sim::isJetDrone(targets.list[size_t(h.unit)].kind) ? jetShahed_ : shahed_, h.gain,
                                           h.pan, h.pitch, true))
      droneVoices_.push_back({h.unit, voice});
  }
}

bool GameAudio::init(const std::string& gameDir, float volume, float voiceVolume) {
  std::string error;
  if (gameDir.empty() || !bank_.load(gameDir, error)) {
    if (!gameDir.empty()) std::fprintf(stderr, "%s, no sound\n", error.c_str());
    return false;
  }
  voiceVolume_ = std::clamp(voiceVolume, 0.0f, 1.5f);
  // The second rocket launch has a second launch three seconds after the first, which would be
  // heard just as the rocket hits: only its first two seconds, faded out.
  const std::vector<float>& r2 = bank_.get(snd::Rocket2);
  rocket2_.assign(r2.begin(), r2.begin() + std::min<size_t>(r2.size(), 2 * gamedata::kSoundRate));
  for (size_t i = 0, n = std::min<size_t>(rocket2_.size(), 500); i < n; ++i) rocket2_[rocket2_.size() - 1 - i] *= float(i) / float(n);
  // "Comrade, weapons reloaded": from the pause after "Comrade," (0.575 s), faded in over 8 ms.
  const std::vector<float>& rl = bank_.get(snd::Reloaded);
  const size_t cut = size_t(0.575f * float(gamedata::kSoundRate)), fadeIn = 88;
  if (rl.size() > cut + fadeIn) {
    reloaded_.assign(rl.begin() + long(cut), rl.end());
    for (size_t i = 0; i < fadeIn; ++i) reloaded_[i] *= float(i) / float(fadeIn);
  }
  makeShahed();
  makeJetShahed();
  loadSqueak();
  if (!mixer_.open(volume)) return false;
  std::printf("Sound: %d blocks of the original\n", bank_.count());
  return true;
}

void GameAudio::loadSqueak() {
  // The remake's data beside the program (or in its source tree): mono, 16-bit, 22050 Hz.
  std::vector<std::string> paths;
  if (char* base = SDL_GetBasePath()) {
    paths.push_back(std::string(base) + "../data/sounds/squeak.wav");
    paths.push_back(std::string(base) + "data/sounds/squeak.wav");
    SDL_free(base);
  }
  paths.push_back(HIND_DATA_DIR "/sounds/squeak.wav");
  for (const std::string& p : paths) {
    SDL_AudioSpec spec;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(p.c_str(), &spec, &buf, &len)) continue;
    if (spec.format == AUDIO_S16LSB && spec.channels == 1 && spec.freq == gamedata::kSpeechRate) {
      squeak_.resize(len / 2);
      for (size_t i = 0; i < squeak_.size(); ++i) squeak_[i] = float(int16_t(buf[2 * i] | buf[2 * i + 1] << 8)) / 32768.0f;
    }
    SDL_FreeWAV(buf);
    return;
  }
}

void GameAudio::squeak() {
  if (ready() && !squeak_.empty()) mixer_.play(squeak_, 0.7f, 0.0f, float(gamedata::kSpeechRate) / float(gamedata::kSoundRate));
}

void GameAudio::startFlight(bool mission, bool ukraine) {
  stop();
  ukraine_ = ukraine;
  started_ = false;
  cleared_ = false;
  overtorque_ = 0.0f;
  cdTorque_ = cdRotor_ = cdPullUp_ = cdThreat_ = cdGroundFire_ = cdFriendly_ = cdKill_ = cdCreak_ = cdHit_ = 0.0f;
  if (mission) say({snd::ClearedTakeOff}, 2.0f);
}

void GameAudio::stop() {
  mixer_.stopAll();
  mixer_.pause(false);
  rotor_ = gun_ = cannon_ = turret_ = voice_ = 0;
  droneVoices_.clear();
  lines_.clear();
  linePart_ = 0;
  lineGap_ = 0.0f;
  delayed_.clear();
}

void GameAudio::say(std::vector<int> parts, float wait) {
  if (!ready()) return;
  // The same words already waiting are not said twice; a long queue drops its oldest.
  for (const Line& l : lines_)
    if (l.parts == parts) return;
  if (lines_.size() >= 4) lines_.erase(lines_.begin() + (linePart_ > 0 ? 1 : 0));
  lines_.push_back({std::move(parts), wait});
}

void GameAudio::sayOnce(int id, float& cooldown, float again) {
  if (cooldown > 0.0f) return;
  say({id});
  cooldown = again;
}

float GameAudio::pan(glm::vec3 pos) const {
  const glm::vec3 d = pos - scene_.ear;
  const float len = glm::length(d);
  return len < 1.0f ? 0.0f : std::clamp(glm::dot(d / len, scene_.earRight), -1.0f, 1.0f) * 0.8f;
}

void GameAudio::positioned(int id, glm::vec3 pos, float gain, float ref) {
  const float d = glm::distance(pos, scene_.ear);
  const float g = gain * std::min(1.0f, ref / std::max(d, 1.0f));
  if (g >= 0.01f) mixer_.play(sample(id), g, pan(pos));
}

void GameAudio::at(int id, glm::vec3 pos, float gain, float ref, float later) {
  if (!ready()) return;
  const float d = glm::distance(pos, scene_.ear);
  if (gain * ref / std::max(d, 1.0f) < 0.01f) return;
  delayed_.push_back({id, pos, gain, ref, time_ + d / kSpeedOfSound + later});
}

int GameAudio::here(int id, float gain, bool loop) {
  float g = gain, p = 0.0f;
  if (!scene_.inside) {
    g *= std::min(1.0f, 30.0f / std::max(glm::distance(heliPos_, scene_.ear), 1.0f));
    p = pan(heliPos_);
  }
  return mixer_.play(sample(id), g, p, 1.0f, loop);
}

void GameAudio::keep(int& voice, int id, bool on, float gain, float pitch) {
  if (!on) {
    mixer_.stop(voice);
    voice = 0;
    return;
  }
  float g = gain, p = 0.0f;
  if (!scene_.inside) {
    g *= std::min(1.0f, 30.0f / std::max(glm::distance(heliPos_, scene_.ear), 1.0f));
    p = pan(heliPos_);
  }
  if (!voice || !mixer_.playing(voice)) voice = mixer_.play(sample(id), g, p, pitch, true);
  else mixer_.set(voice, g, p, pitch);
}

void GameAudio::update(float dt, const Scene& scene, const sim::Helicopter& heli, const sim::Weapons& weapons,
                       const sim::EnemyFire& enemy) {
  if (!ready()) return;
  mixer_.pause(scene.paused);
  if (scene.paused) return;
  scene_ = scene;
  heliPos_ = heli.pos;
  time_ += dt;
  for (float* cd : {&cdTorque_, &cdRotor_, &cdPullUp_, &cdThreat_, &cdGroundFire_, &cdFriendly_, &cdKill_, &cdCreak_, &cdHit_})
    *cd = std::max(0.0f, *cd - dt);

  const bool onGround = heli.weightOnWheels > 0.0f;
  int incoming = 0;
  bool incomingIr = false;
  for (const sim::EnemyShot& s : enemy.shots())
    if ((s.kind == sim::EnemyRound::Strela || s.kind == sim::EnemyRound::Rapier) && s.lock == -1 && !s.lost) {
      ++incoming;
      incomingIr = incomingIr || s.kind == sim::EnemyRound::Strela;
    }
  // The threat warning knocked out warns of nothing.
  if (!heli.works(sim::System::Twi)) incoming = 0;
  if (!started_) {
    // What it is at the start is no news.
    started_ = true;
    // A mission begun in the air is not cleared to take off.
    if (!onGround)
      for (auto it = lines_.begin(); it != lines_.end(); ++it)
        if (it->parts == std::vector<int>{snd::ClearedTakeOff}) {
          lines_.erase(it);
          break;
        }
    gearDown_ = heli.gearDown, onGround_ = onGround, crashed_ = heli.crashed;
    for (int k = 0; k < 2; ++k) engineOut_[k] = heli.engineOut(k), engineFire_[k] = heli.engineFire(k);
    fuelLow_ = false;
    flares_ = enemy.flares, chaff_ = enemy.chaff;
    autoFlares_ = enemy.autoFlares, autoChaff_ = enemy.autoChaff;
    armed_ = weapons.armed(), selected_ = weapons.selected(), wso_ = scene.wso;
    waypoint_ = scene.waypoint;
    wsoTarget_ = -1;
    threat_ = false, threatClass_ = 0;
    incoming_ = incoming;
  }

  // The engines and the blades: louder and higher as the rotor runs up, a little louder with
  // more torque; from outside fainter with the distance.
  {
    const float level = std::clamp(heli.rotorRpm / 100.0f, 0.0f, 1.15f);
    const float gain = heli.crashed ? 0.0f : 0.5f * std::pow(level, 1.5f) * (0.85f + 0.15f * heli.torque / 100.0f);
    keep(rotor_, snd::Rotor, gain > 0.002f, gain, 0.55f + 0.45f * level);
  }
  // The guns run while rounds leave them.
  keep(gun_, snd::Gun, time_ - gunFired_ < 0.1f && !heli.crashed, 0.55f);
  keep(cannon_, snd::Cannon, time_ - cannonFired_ < 0.1f && !heli.crashed, 0.6f);
  keep(doorGun_, snd::MachineGun, time_ - doorFired_ < 0.1f && !heli.crashed, 0.5f, 1.3f);
  // The turret's motors when it swings round, not while it holds a target as the helicopter
  // pitches and turns under it (the WSO's tracking would rattle at every touch of the cyclic).
  keep(turret_, snd::Turret, weapons.turretSlewing() && scene_.inside, 0.02f);  // barely heard, as in the original

  if (heli.gearDown != gearDown_) here(snd::Gear, 0.6f);
  gearDown_ = heli.gearDown;
  // Touching down: from a light bump to a crash by how fast it sank.
  if (!onGround) sinkRate_ = std::max(0.0f, -heli.vel.y);
  if (onGround && !onGround_ && !heli.crashed) {
    const int k = sinkRate_ < 1.0f ? 0 : sinkRate_ < 2.0f ? 1 : sinkRate_ < 3.0f ? 2 : sinkRate_ < 4.5f ? 3 : 4;
    here(snd::Touchdown + k, 0.45f + 0.1f * float(k));
  }
  onGround_ = onGround;
  if (heli.crashed && !crashed_) {
    here(snd::HeliCrash, 1.0f);
    lines_.clear();
    linePart_ = 0;
  }
  crashed_ = heli.crashed;
  if (scene.gLoad > 2.4f && cdCreak_ <= 0.0f) {
    here(snd::Airframe, 0.5f);
    cdCreak_ = 4.0f;
  }
  if (enemy.flares < flares_ || enemy.chaff < chaff_) here(snd::Flare, 0.6f);
  flares_ = enemy.flares, chaff_ = enemy.chaff;
  // The launchers switched on and off.
  if (enemy.autoChaff != autoChaff_) say({enemy.autoChaff ? snd::AutoChaffOn : snd::AutoChaffOff});
  if (enemy.autoFlares != autoFlares_) say({enemy.autoFlares ? snd::AutoFlareOn : snd::AutoFlareOff});
  autoChaff_ = enemy.autoChaff, autoFlares_ = enemy.autoFlares;

  // The WSO: on and off, the weapon he arms.
  if (scene.wso != wso_) say({scene.wso ? snd::WsoActive : snd::WsoOff});
  wso_ = scene.wso;
  if (weapons.armed() != armed_ || (weapons.armed() && weapons.selected() != selected_))
    say({weapons.armed() ? armedLine(weapons.selected()) : weapons.madeSafe() ? snd::WeaponSafe : snd::NavigationMode});
  armed_ = weapons.armed(), selected_ = weapons.selected();
  // The gun's ammunition again after a minute on the ground.
  if (weapons.reloads() > reloads_) say({snd::Reloaded});
  reloads_ = weapons.reloads();
  // ... and what the new target he has is.
  if (scene.wsoTarget >= 0 && scene.wsoTarget != wsoTarget_) {
    const int line = gamedata::wsoTypeLine(scene.wsoTargetObject, scene.wsoTargetEnemy);
    if (line >= 0) {
      // A newer target replaces one not yet said.
      for (size_t k = linePart_ > 0 ? 1 : 0; k < lines_.size();)
        if (lines_[k].parts.size() == 1 && lines_[k].parts[0] >= snd::WsoTypeFirst && lines_[k].parts[0] < snd::CargoDoorsOpen)
          lines_.erase(lines_.begin() + long(k));
        else
          ++k;
      say({line});
    }
  }
  wsoTarget_ = scene.wsoTarget;

  // Natasha.
  if (!heli.crashed) {
    for (int k = 0; k < 2; ++k) {
      if (heli.engineFire(k) && !engineFire_[k]) {
        here(snd::Warning, 0.5f);
        say({snd::EngineFire});
      }
      if (heli.engineOut(k) && !engineOut_[k]) say({k == 0 ? snd::LeftEngineFailure : snd::RightEngineFailure});
    }
    const bool fuelLow = !heli.infiniteFuel && heli.fuel < 0.12f * sim::Helicopter::kFuelFull;
    if (fuelLow && !fuelLow_) say({snd::FuelLow});
    fuelLow_ = fuelLow;
    if (!onGround) {
      overtorque_ = heli.torque > 115.0f ? overtorque_ + dt : 0.0f;
      if (overtorque_ > 1.5f) sayOnce(snd::Overtorque, cdTorque_, 10.0f);
      if (heli.rotorRpm < 90.0f) sayOnce(snd::LowRotorRpm, cdRotor_, 8.0f);
      if (heli.radarAltitude() < 40.0f && heli.vel.y < -10.0f && cdPullUp_ <= 0.0f) {
        here(snd::Warning, 0.5f);
        sayOnce(snd::PullUp, cdPullUp_, 4.0f);
      }
    }
    if (incoming > incoming_) {
      here(snd::Warning, 0.5f);
      say({incomingIr ? snd::IncomingIr : snd::IncomingRf});
    }
    const int cls = scene.threat.active ? int(scene.threat.cls) : 0;
    if (cls && (!threat_ || cls != threatClass_) && cdThreat_ <= 0.0f) {
      say({threatLine(cls)});
      cdThreat_ = 10.0f;
    }
    threat_ = cls != 0, threatClass_ = cls;
  }
  for (int k = 0; k < 2; ++k) engineOut_[k] = heli.engineOut(k), engineFire_[k] = heli.engineFire(k);
  incoming_ = incoming;

  // Ground control: the heading to each new waypoint, the landing at home.
  if (scene.waypoint >= 0 && waypoint_ >= 0 && scene.waypoint != waypoint_ && !heli.crashed) {
    const int h = (int(std::lround(scene.waypointBearing)) % 360 + 360) % 360;
    say({snd::Vector, snd::DigitFirst + h / 100, snd::DigitMiddle + h / 10 % 10, snd::DigitLast + h % 10});
  }
  waypoint_ = scene.waypoint;
  if (scene.waypoint == 0 && scene.waypointDistance < 3000.0f && !onGround && !cleared_ && !heli.crashed) {
    say({snd::ClearedLand});
    cleared_ = true;
  }

  // The voices, one after another; the parts of a line follow closely.
  if (voice_ && mixer_.playing(voice_)) {
  } else if (!lines_.empty()) {
    voice_ = 0;
    Line& l = lines_.front();
    if (l.wait > 0.0f) {
      l.wait -= dt;
    } else if ((lineGap_ -= dt) <= 0.0f) {
      voice_ = mixer_.play(sample(l.parts[linePart_]), voiceVolume_);
      if (++linePart_ >= l.parts.size()) {
        lines_.pop_front();
        linePart_ = 0;
        lineGap_ = 0.5f;
      } else {
        lineGap_ = 0.0f;
      }
    }
  }

  // Sounds from out there that have come this far.
  for (size_t i = 0; i < delayed_.size();) {
    if (time_ >= delayed_[i].at) {
      positioned(delayed_[i].id, delayed_[i].pos, delayed_[i].gain, delayed_[i].ref);
      delayed_[i] = delayed_.back();
      delayed_.pop_back();
    } else {
      ++i;
    }
  }
}

void GameAudio::fired(sim::WeaponType w) {
  if (!ready()) return;
  switch (w) {
    case sim::WeaponType::YakB: gunFired_ = time_; break;
    case sim::WeaponType::Upk23: cannonFired_ = time_; break;
    case sim::WeaponType::DoorGun: doorFired_ = time_; break;
    case sim::WeaponType::S8:
    case sim::WeaponType::S5:
      // A ripple of rockets is a rush of launches, not a sound for each.
      if (time_ - rocketSound_ > 0.11f) {
        here((rocketAlt_ ^= 1) ? snd::Rocket : snd::Rocket2, 0.5f);
        rocketSound_ = time_;
      }
      break;
    case sim::WeaponType::Shturm: here(snd::AirToSurface, 0.6f); break;
    case sim::WeaponType::Mine: here(snd::MineLay, 0.7f); break;
    case sim::WeaponType::R60: here(snd::AirToAir, 0.7f); break;
    case sim::WeaponType::Interceptor: here(snd::BombRelease, 0.35f); break;  // a small drone let go off its rack
    default: here(snd::BombRelease, 0.6f); break;
  }
}

void GameAudio::impact(const sim::Impact& i, const sim::Targets& targets, const world::Terrain& terrain) {
  if (!ready()) return;
  const sim::Vehicle* v = i.target >= 0 && i.target < int(targets.list.size()) ? &targets.list[size_t(i.target)] : nullptr;
  const sim::VehicleType* t = v ? &sim::vehicleType(v->kind) : nullptr;
  if (v && v->side == sim::Side::Ours && cdFriendly_ <= 0.0f) {
    // Ground control does not like it.
    int line = snd::StopFiringForces + (friendly_++ % 2);
    if (t->cls == sim::UnitClass::Structure) line = snd::CeaseInstallations;
    if (t->cls == sim::UnitClass::Aircraft || t->cls == sim::UnitClass::Helicopter) line = snd::StopFriendlyAircraft;
    say({line});
    cdFriendly_ = 12.0f;
  }
  if (i.destroyed && v) {
    if (v->side == sim::Side::Enemy && cdKill_ <= 0.0f) {
      static const int lines[] = {snd::GoodKill, snd::TargetHit, snd::GoodShot};
      say({lines[kill_++ % 3]}, 0.4f);
      cdKill_ = 6.0f;
    }
    if (t->cls == sim::UnitClass::Aircraft || t->cls == sim::UnitClass::Helicopter) at(snd::HeliCrash, i.pos, 1.0f, 150.0f);
    else if (t->cls != sim::UnitClass::Soldier) at(snd::ExplodeLarge, i.pos, 1.0f, 120.0f);
    return;
  }
  if (i.destroyed) return;
  const bool water = !v && terrain.isWater(i.pos.x, i.pos.z);
  switch (i.type) {
    case sim::WeaponType::YakB:
    case sim::WeaponType::Upk23:
    case sim::WeaponType::DoorGun: break;  // too many to hear one by one
    case sim::WeaponType::S5:
    case sim::WeaponType::Interceptor: at(water ? snd::Splash : snd::ExplodeSmall, i.pos, 0.8f, 60.0f); break;
    case sim::WeaponType::S8:
    case sim::WeaponType::R60:
    case sim::WeaponType::Shturm: at(water ? snd::Splash : snd::ExplodeMedium, i.pos, 0.9f, 70.0f); break;
    default:
      if (sim::isBomb(i.type)) {
        const bool big = i.type == sim::WeaponType::Fab500 || i.type == sim::WeaponType::Zab500;
        at(water ? snd::ExplodeWater : big ? snd::ExplodeBig : snd::ExplodeLarge, i.pos, 1.0f, 150.0f);
      }
      break;
  }
}

void GameAudio::enemyEvent(const sim::EnemyEvent& e, const sim::Helicopter& heli) {
  if (!ready()) return;
  const float near = glm::distance(e.pos, heli.pos);
  switch (e.type) {
    case sim::EnemyEvent::Muzzle:
      if (e.kind == sim::EnemyRound::Shell) {
        for (int k = 0; k < 3; ++k) at(snd::Flak, e.pos, 0.8f, 60.0f, 0.22f * float(k));
      } else {
        at(snd::MachineGun, e.pos, 0.7f, 40.0f);
      }
      if (near < 2000.0f && e.atHeli) sayOnce(snd::EnemyGroundFire, cdGroundFire_, 45.0f);
      break;
    case sim::EnemyEvent::Launch:
      if (e.kind == sim::EnemyRound::Rpg) at(snd::Rocket2, e.pos, 0.6f, 40.0f);
      else at(snd::SamLaunch, e.pos, 0.8f, 80.0f);
      break;
    case sim::EnemyEvent::Burst:
      if (near < 20.0f) here(snd::MediumHit, 0.9f);
      else at(e.kind == sim::EnemyRound::Shell ? snd::ExplodeSmall : snd::ExplodeMedium, e.pos,
              e.kind == sim::EnemyRound::Shell ? 0.4f : 0.8f, 60.0f);
      break;
    case sim::EnemyEvent::Hit:
      if (cdHit_ <= 0.0f) {
        here(snd::SmallHit, 0.7f);
        cdHit_ = 0.15f;
      }
      break;
  }
}


void GameAudio::speak(const std::vector<uint8_t>& raw) {
  if (!ready()) return;
  stopSpeech();
  if (speech_.size() >= 3) speech_.pop_front();
  std::vector<float>& s = speech_.emplace_back(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) s[i] = (float(raw[i]) - 128.0f) / 128.0f;
  // Twice the rate of the game's sounds.
  speechVoice_ = mixer_.play(s, voiceVolume_, 0.0f, float(gamedata::kSpeechRate) / float(gamedata::kSoundRate));
  mixer_.keepPlaying(speechVoice_);
}

void GameAudio::music(const std::vector<uint8_t>& raw, bool loop) {
  if (!ready()) return;
  stopMusic();
  if (music_.size() >= 2) music_.pop_front();
  auto& [l, r] = music_.emplace_back();
  l.resize(raw.size() / 2), r.resize(raw.size() / 2);
  for (size_t i = 0; i < l.size(); ++i) {
    l[i] = (float(raw[2 * i]) - 128.0f) / 128.0f;
    r[i] = (float(raw[2 * i + 1]) - 128.0f) / 128.0f;
  }
  musicVoices_ = mixer_.playStereo(l, r, musicDucked_ ? kMusicDucked : kMusicGain, float(gamedata::kSpeechRate) / float(gamedata::kSoundRate), loop);
  mixer_.keepPlaying(musicVoices_.first);
  mixer_.keepPlaying(musicVoices_.second);
}

void GameAudio::stopMusic() {
  if (musicVoices_.first) mixer_.stop(musicVoices_.first), mixer_.stop(musicVoices_.second);
  musicVoices_ = {0, 0};
}

void GameAudio::duckMusic(bool duck) {
  if (duck == musicDucked_) return;
  musicDucked_ = duck;
  const float g = duck ? kMusicDucked : kMusicGain;
  const float pitch = float(gamedata::kSpeechRate) / float(gamedata::kSoundRate);
  if (musicVoices_.first) mixer_.set(musicVoices_.first, g, -1.0f, pitch), mixer_.set(musicVoices_.second, g, 1.0f, pitch);
}

void GameAudio::stopSpeech() {
  if (speechVoice_) mixer_.stop(speechVoice_);
  speechVoice_ = 0;
}

}  // namespace audio
