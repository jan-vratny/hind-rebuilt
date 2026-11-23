#include "net/mirror.hpp"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace net {

namespace {

void writeDamage(Writer& w, const sim::Helicopter::Damage& d) {
  w.pod(d.airframe), w.pod(d.engine), w.pod(d.tailRotor), w.pod(d.fuelLeak), w.pod(d.shotDown), w.pod(d.failed);
  w.str(d.lastHitBy);
}

void readDamage(Reader& r, sim::Helicopter::Damage& d) {
  r.pod(d.airframe), r.pod(d.engine), r.pod(d.tailRotor), r.pod(d.fuelLeak), r.pod(d.shotDown), r.pod(d.failed);
  r.str(d.lastHitBy);
}

void writeStrings(Writer& w, const std::vector<std::string>& v) {
  w.pod(uint32_t(v.size()));
  for (const std::string& s : v) w.str(s);
}

void readStrings(Reader& r, std::vector<std::string>& v) {
  const uint32_t n = r.get<uint32_t>();
  v.clear();
  for (uint32_t i = 0; i < n && r.ok(); ++i) r.str(v.emplace_back());
}

}  // namespace

void Mirror::writeHeli(Writer& w, const sim::Helicopter& h) {
  w.pod(h.pos), w.pod(h.vel), w.pod(h.orient), w.pod(h.omega);
  w.pod(h.model), w.pod(h.mass), w.pod(h.gearDown), w.pod(h.gearPos), w.pod(h.brakes), w.pod(h.crashed);
  w.pod(h.crashVel), w.pod(h.collective), w.pod(h.torque), w.pod(h.fuel), w.pod(h.engineRpm), w.pod(h.rotorRpm), w.pod(h.rotorFree);
  w.pod(h.rotorAngle), w.pod(h.tailRotorAngle), w.pod(h.weightOnWheels);
  w.str(h.message), w.pod(h.messageTime);
  writeDamage(w, h.damage);
}

void Mirror::readHeli(Reader& r, sim::Helicopter& h) {
  r.pod(h.pos), r.pod(h.vel), r.pod(h.orient), r.pod(h.omega);
  r.pod(h.model), r.pod(h.mass), r.pod(h.gearDown), r.pod(h.gearPos), r.pod(h.brakes), r.pod(h.crashed);
  r.pod(h.crashVel), r.pod(h.collective), r.pod(h.torque), r.pod(h.fuel), r.pod(h.engineRpm), r.pod(h.rotorRpm), r.pod(h.rotorFree);
  r.pod(h.rotorAngle), r.pod(h.tailRotorAngle), r.pod(h.weightOnWheels);
  r.str(h.message), r.pod(h.messageTime);
  readDamage(r, h.damage);
}


void StartInfo::write(Writer& w) const {
  w.pod(generation), w.str(zone), w.str(mission), w.pod(loadout), w.pod(home), w.pod(heading), w.pod(flightModel);
  w.pod(hour), w.pod(weather), w.vec(route), w.vec(routeAlts), w.pod(targets), w.pod(slot), w.str(game);
  w.pod(infiniteWeapons), w.pod(doorReloads), w.pod(noCrash);
  w.pod(airStart), w.pod(infiniteFuel), w.pod(airAltitude);
}

bool StartInfo::read(Reader& r) {
  r.pod(generation), r.str(zone), r.str(mission), r.pod(loadout), r.pod(home), r.pod(heading), r.pod(flightModel);
  r.pod(hour), r.pod(weather), r.vec(route), r.vec(routeAlts), r.pod(targets), r.pod(slot), r.str(game);
  r.pod(infiniteWeapons), r.pod(doorReloads), r.pod(noCrash);
  r.pod(airStart), r.pod(infiniteFuel), r.pod(airAltitude);
  return r.ok();
}

const char* chatText(int number) {
  static const char* texts[30] = {
      "BREAK LEFT",  "BREAK RIGHT",  "BREAK OFF",   "SLOW DOWN",      "FLY FASTER",          "TURN LEFT",
      "TURN RIGHT",  "STRAIGHT AHEAD", "GO AROUND AGAIN", "LET'S GO HOME",
      "TARGET LEFT", "TARGET RIGHT", "TARGET AHEAD", "TARGET BEHIND", "TAKE DEFENCES",       "TAKE GROUND TARGETS",
      "TAKE AIR TARGETS", "HOLD YOUR FIRE", "GOING ROUND AGAIN", "LET'S GO HOME",
      "BREAK LEFT",  "BREAK RIGHT",  "BREAK OFF",   "REGROUP",        "TAKE DEFENCES",       "TAKE GROUND TARGETS",
      "TAKE AIR TARGETS", "LOW ON WEAPONS", "GOING ROUND AGAIN", "I'M GOING HOME"};
  return number >= 1 && number <= 30 ? texts[number - 1] : "";
}

HindPose HindPose::of(const sim::Helicopter& h) {
  HindPose p;
  p.pos = h.pos, p.orient = h.orient, p.rotorAngle = h.rotorAngle, p.tailRotorAngle = h.tailRotorAngle;
  p.gearPos = h.gearPos, p.rotorRpm = h.rotorRpm, p.crashed = h.crashed ? 1 : 0;
  return p;
}

void HindPose::apply(sim::Helicopter& h) const {
  h.pos = pos, h.orient = orient, h.rotorAngle = rotorAngle, h.tailRotorAngle = tailRotorAngle;
  h.gearPos = gearPos, h.gearDown = gearPos > 0.5f, h.rotorRpm = rotorRpm, h.crashed = crashed != 0;
}

BowmanPose BowmanPose::of(const sim::Bowman& b) {
  BowmanPose p;
  p.pos = b.pos, p.yaw = b.yaw, p.pitch = b.pitch, p.stride = b.stride, p.walking = b.walking ? 1 : 0;
  p.draw = b.state == sim::Bowman::State::Drawing ? b.draw : 0.0f;
  p.flying = b.state == sim::Bowman::State::Flying ? 1 : 0;
  p.arrowPos = b.arrowPos, p.arrowVel = b.arrowVel;
  p.weapon = uint8_t(b.weapon), p.dead = b.dead() ? 1 : 0;
  p.lockHind = b.locked ? b.aimHind : int8_t(-1);
  p.rocketHind = b.rocket.flying && !b.rocket.decoyed() ? b.rocket.hind : int8_t(-1);
  return p;
}

Mirror::Unit Mirror::unitOf(const sim::Vehicle& v) {
  return {v.pos, v.vel, v.heading, v.health, v.delay, uint32_t(v.next), v.destroyed, v.aboard, v.struck, v.hover};
}

void Mirror::write(Writer& w, uint32_t generation, const World& world, const Frame& frame, const Events& events) {
  w.pod(generation);
  w.pod(frame.simTime), w.pod(frame.paused), w.pod(frame.controls), w.pod(frame.cyclicKeys), w.pod(frame.doorsTaken), w.pod(frame.doorMove);
  writeStrings(w, frame.page);
  writeDamage(w, frame.wingmanDamage), w.str(frame.wingmanMessage), w.pod(frame.wingmanFlares), w.pod(frame.wingmanChaff);
  w.vec(frame.others), w.vec(frame.bowmen), w.vec(frame.wingStores), w.vec(frame.wingShots);
  writeHeli(w, world.heli);

  const sim::Weapons& wp = world.weapons;
  w.pod(wp.yakB_), w.pod(wp.left_), w.pod(wp.doorRounds_), w.pod(wp.doorReload_), w.pod(wp.selected_), w.pod(wp.armed_), w.pod(wp.madeSafe_);
  w.pod(wp.turretAz_), w.pod(wp.turretEl_), w.pod(wp.reloads_);
  w.pod(wp.door_[0].target), w.pod(wp.door_[1].target);
  w.pod(wp.gunner_.manned), w.pod(wp.gunner_.armed), w.pod(wp.gunner_.weapon), w.pod(wp.doorGunnersFree), w.pod(wp.doorAim_);
  w.vec(wp.shots);

  const sim::EnemyFire& en = world.enemy;
  w.pod(en.flares), w.pod(en.chaff), w.pod(en.jammer), w.pod(en.autoFlares), w.pod(en.autoChaff);
  w.vec(en.shots_), w.vec(en.flareList_), w.vec(en.chaffList_);

  w.pod(world.troops.doors_);
  w.vec(world.troops.walkers_);
  w.pod(world.wso.enabled_), w.pod(world.wso.target_);
  w.pod(world.spotter.designated_);
  const sim::MissionState& m = world.mission;
  w.pod(m.drillLanding_), w.pod(m.active_), w.pod(m.finished_), w.pod(m.waypoint_), w.pod(m.reconLeft_);

  // The units that changed: index and state.
  const std::vector<sim::Vehicle>& list = world.targets.list;
  if (sent_.size() != list.size()) sent_.assign(list.size(), Unit{glm::vec3(NAN), {}, 0, 0, 0, 0, 0, 0, 0, 0});
  std::vector<uint8_t> changed;
  Writer units;
  uint32_t count = 0;
  for (size_t i = 0; i < list.size(); ++i) {
    const Unit u = unitOf(list[i]);
    Unit& s = sent_[i];
    const bool same = glm::distance(u.pos, s.pos) < 0.02f && glm::distance(u.vel, s.vel) < 0.02f &&
                      std::fabs(u.heading - s.heading) < 0.002f && u.health == s.health && (u.delay <= 0.0f) == (s.delay <= 0.0f) &&
                      u.next == s.next && u.destroyed == s.destroyed && u.aboard == s.aboard && u.struck == s.struck && u.hover == s.hover;
    if (same) continue;
    s = u;
    units.pod(uint32_t(i)), units.pod(u);
    ++count;
  }
  w.pod(count);
  w.bytes.insert(w.bytes.end(), units.bytes.begin(), units.bytes.end());
  // The mines lying about, when there are more or fewer.
  const bool mines = world.targets.mines.size() != mines_;
  w.pod(mines);
  if (mines) w.vec(world.targets.mines), mines_ = world.targets.mines.size();

  w.vec(events.impacts), w.vec(events.enemy), w.vec(events.fired), w.vec(events.radio), w.vec(events.effects);
}

bool Mirror::read(Reader& r, uint32_t& generation, World& world, Frame& frame, Events& events) {
  r.pod(generation);
  r.pod(frame.simTime), r.pod(frame.paused), r.pod(frame.controls), r.pod(frame.cyclicKeys), r.pod(frame.doorsTaken), r.pod(frame.doorMove);
  readStrings(r, frame.page);
  readDamage(r, frame.wingmanDamage), r.str(frame.wingmanMessage), r.pod(frame.wingmanFlares), r.pod(frame.wingmanChaff);
  r.vec(frame.others), r.vec(frame.bowmen), r.vec(frame.wingStores), r.vec(frame.wingShots);
  readHeli(r, world.heli);

  sim::Weapons& wp = world.weapons;
  r.pod(wp.yakB_), r.pod(wp.left_), r.pod(wp.doorRounds_), r.pod(wp.doorReload_), r.pod(wp.selected_), r.pod(wp.armed_), r.pod(wp.madeSafe_);
  r.pod(wp.turretAz_), r.pod(wp.turretEl_), r.pod(wp.reloads_);
  r.pod(wp.door_[0].target), r.pod(wp.door_[1].target);
  r.pod(wp.gunner_.manned), r.pod(wp.gunner_.armed), r.pod(wp.gunner_.weapon), r.pod(wp.doorGunnersFree), r.pod(wp.doorAim_);
  // The gunner's game shows his own weapon, not the pilot's.
  if (wp.gunner_.manned) wp.selected_ = wp.gunner_.weapon, wp.armed_ = wp.gunner_.armed;
  r.vec(wp.shots);

  sim::EnemyFire& en = world.enemy;
  r.pod(en.flares), r.pod(en.chaff), r.pod(en.jammer), r.pod(en.autoFlares), r.pod(en.autoChaff);
  r.vec(en.shots_), r.vec(en.flareList_), r.vec(en.chaffList_);

  r.pod(world.troops.doors_);
  r.vec(world.troops.walkers_);
  r.pod(world.wso.enabled_), r.pod(world.wso.target_);
  r.pod(world.spotter.designated_);
  sim::MissionState& m = world.mission;
  r.pod(m.drillLanding_), r.pod(m.active_), r.pod(m.finished_), r.pod(m.waypoint_), r.pod(m.reconLeft_);

  std::vector<sim::Vehicle>& list = world.targets.list;
  const uint32_t count = r.get<uint32_t>();
  for (uint32_t k = 0; k < count && r.ok(); ++k) {
    const uint32_t i = r.get<uint32_t>();
    const Unit u = r.get<Unit>();
    if (i >= list.size()) continue;
    sim::Vehicle& v = list[i];
    if (u.destroyed && !v.destroyed) v.sinceDestroyed = 0.0f;
    v.pos = u.pos, v.vel = u.vel, v.heading = u.heading, v.health = u.health, v.delay = u.delay;
    v.next = u.next, v.destroyed = u.destroyed, v.aboard = u.aboard, v.struck = u.struck, v.hover = u.hover;
  }
  if (r.get<bool>()) r.vec(world.targets.mines);

  r.vec(events.impacts), r.vec(events.enemy), r.vec(events.fired), r.vec(events.radio), r.vec(events.effects);
  // The units hit are the gunner's own indices too; one past the end would be a different battlefield.
  for (const sim::Impact& i : events.impacts)
    if (i.target >= int(list.size())) return false;
  if (wp.door_[0].target >= int(list.size()) || wp.door_[1].target >= int(list.size()) ||
      world.wso.target_ >= int(list.size()) || world.spotter.designated_ >= int(list.size()))
    return false;
  return r.ok() && r.done();
}

void Mirror::advance(World& world, float dt) {
  sim::Helicopter& h = world.heli;
  if (!h.crashed) {
    h.pos += h.vel * dt;
    const glm::quat w{0.0f, h.omega.x, h.omega.y, h.omega.z};
    h.orient = glm::normalize(h.orient + (h.orient * w) * (0.5f * dt));
    // As Helicopter::step turns them (3.2 revolutions a second).
    const float turn = 3.2f * 2.0f * glm::pi<float>() * dt;
    h.rotorAngle = std::fmod(h.rotorAngle + turn, 2.0f * glm::pi<float>());
    h.tailRotorAngle = std::fmod(h.tailRotorAngle + turn * 5.6f, 2.0f * glm::pi<float>());
  }
  for (sim::Vehicle& v : world.targets.list) {
    if (v.destroyed) v.sinceDestroyed += dt;
    if (v.fallingDrone()) v.pos += v.vel * dt;
    else if (v.moving() && v.delay <= 0.0f) v.pos += v.vel * dt;
  }
  for (sim::Projectile& p : world.weapons.shots) p.pos += p.vel * dt, p.age += dt;
  for (sim::EnemyShot& s : world.enemy.shots_) s.pos += s.vel * dt, s.age += dt;
  for (sim::Flare& f : world.enemy.flareList_) f.pos += f.vel * dt, f.age += dt;
  for (sim::Chaff& c : world.enemy.chaffList_) c.pos += c.vel * dt, c.age += dt;
}

void Mirror::route(const sim::MissionState& m, std::vector<glm::vec2>& route, std::vector<float>& alts) {
  route = m.route_;
  alts = m.routeAlts_;
}

void Mirror::setRoute(sim::MissionState& m, const std::vector<glm::vec2>& route, const std::vector<float>& alts) {
  m.route_ = route;
  m.routeAlts_ = alts;
}

}  // namespace net
