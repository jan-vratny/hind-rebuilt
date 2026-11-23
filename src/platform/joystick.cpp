#include "platform/joystick.hpp"

#include <algorithm>
#include <cmath>

#include "platform/door_gun.hpp"

namespace platform::joy {

namespace {

const char* const kKeys[kControls] = {"cyclic_x", "cyclic_y", "collective", "pedal", "look_x", "look_y"};
const char* const kLabels[kControls] = {"Stick left-right", "Stick fore-aft", "Collective", "Pedals", "Look left-right", "Look up-down"};
// The raw axis's way that is + (SDL: a stick forward and a throttle up are -).
const float kSign[kControls] = {1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f};
const int kDefaultAxis[kControls] = {0, 1, 2, 3, -1, -1};

}  // namespace

const char* key(Control c) { return kKeys[c]; }
const char* label(Control c) { return kLabels[c]; }
float sign(Control c) { return kSign[c]; }

Map load(const Config& cfg) {
  Map m;
  for (int c = 0; c < kControls; ++c) {
    const std::string k = kKeys[c];
    m.axes[c].axis = cfg.getInt("joystick", k + "_axis", kDefaultAxis[c]);
    m.axes[c].device = cfg.getString("joystick", k + "_device", "");
    m.axes[c].invert = cfg.getBool("joystick", "invert_" + k, false);
  }
  m.hat.axis = cfg.getInt("joystick", "look_hat", 0);
  m.hat.device = cfg.getString("joystick", "look_hat_device", "");
  m.device = cfg.getInt("joystick", "device", 0);
  m.deviceName = cfg.getString("joystick", "device_name", "");
  m.mapped = cfg.getBool("joystick", "mapped", false);
  m.doorDevice = cfg.getString("joystick", "door_device", m.doorDevice);
  m.doorYawAxis = cfg.getInt("joystick", "door_yaw_axis", m.doorYawAxis);
  m.doorPitchAxis = cfg.getInt("joystick", "door_pitch_axis", m.doorPitchAxis);
  m.doorFireButton = cfg.getInt("joystick", "door_fire_button", m.doorFireButton);
  m.invertDoorYaw = cfg.getBool("joystick", "invert_door_yaw", false);
  m.invertDoorPitch = cfg.getBool("joystick", "invert_door_pitch", false);
  return m;
}

void save(Config& cfg, const Map& m) {
  for (int c = 0; c < kControls; ++c) {
    const std::string k = kKeys[c];
    cfg.set("joystick", k + "_axis", std::to_string(m.axes[c].axis));
    cfg.set("joystick", k + "_device", m.axes[c].device);
    cfg.set("joystick", "invert_" + k, m.axes[c].invert ? "1" : "0");
  }
  cfg.set("joystick", "look_hat", std::to_string(m.hat.axis));
  cfg.set("joystick", "look_hat_device", m.hat.device);
  cfg.set("joystick", "device", std::to_string(m.device));
  cfg.set("joystick", "device_name", m.deviceName);
  cfg.set("joystick", "mapped", m.mapped ? "1" : "0");
}

void Devices::refresh() {
  for (size_t i = 0; i < list_.size();) {
    if (!SDL_JoystickGetAttached(list_[i].joy)) {
      SDL_JoystickClose(list_[i].joy);
      list_.erase(list_.begin() + long(i));
    } else {
      ++i;
    }
  }
  const int n = SDL_NumJoysticks();
  if (n == count()) return;
  for (int i = 0; i < n; ++i) {
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
    if (std::any_of(list_.begin(), list_.end(), [&](const Device& d) { return d.id == id; })) continue;
    SDL_Joystick* j = SDL_JoystickOpen(i);
    if (!j) continue;
    const char* base = SDL_JoystickName(j);
    std::string name = base && *base ? base : "Joystick";
    for (int k = 2; index(name) >= 0; ++k) name = std::string(base && *base ? base : "Joystick") + " #" + std::to_string(k);
    list_.push_back({j, id, name});
  }
  // In SDL's order (the main device's index is SDL's).
  auto order = [n](const Device& d) {
    for (int i = 0; i < n; ++i)
      if (SDL_JoystickGetDeviceInstanceID(i) == d.id) return i;
    return n;
  };
  std::stable_sort(list_.begin(), list_.end(), [&](const Device& a, const Device& b) { return order(a) < order(b); });
}

void Devices::close() {
  for (Device& d : list_) SDL_JoystickClose(d.joy);
  list_.clear();
}

int Devices::index(const std::string& name) const {
  for (int i = 0; i < count(); ++i)
    if (list_[size_t(i)].name == name) return i;
  return -1;
}

int Devices::sdlIndex(int i) const {
  for (int k = 0; k < SDL_NumJoysticks(); ++k)
    if (SDL_JoystickGetDeviceInstanceID(k) == id(i)) return k;
  return -1;
}

int Devices::main(const Map& map) const {
  std::vector<std::string> names;
  for (const Device& d : list_) names.push_back(d.name);
  return door::pickMain(names, map.deviceName, map.device, map.doorDevice);
}

int Devices::mount(const Map& map) const {
  std::vector<std::string> names;
  for (const Device& d : list_) names.push_back(d.name);
  return door::findMount(names, map.doorDevice);
}

int Devices::of(const Map& map, const Binding& b) const { return b.device.empty() ? main(map) : index(b.device); }

float Devices::raw(int device, int axis) const {
  if (device < 0 || device >= count()) return 0.0f;
  SDL_Joystick* j = get(device);
  if (axis < 0 || axis >= SDL_JoystickNumAxes(j)) return 0.0f;
  return std::max(-1.0f, float(SDL_JoystickGetAxis(j, axis)) / 32767.0f);
}

float Devices::value(const Map& map, Control c) const {
  const Binding& b = map.axes[c];
  return raw(of(map, b), b.axis) * kSign[c] * (b.invert ? -1.0f : 1.0f);
}

Uint8 Devices::hat(const Map& map) const {
  const int d = of(map, map.hat);
  if (d < 0 || map.hat.axis < 0 || map.hat.axis >= SDL_JoystickNumHats(get(d))) return SDL_HAT_CENTERED;
  return SDL_JoystickGetHat(get(d), map.hat.axis);
}

void Learner::start(const Devices& devices) {
  rest_.assign(size_t(devices.count()), {});
  for (int d = 0; d < devices.count(); ++d)
    for (int a = 0; a < SDL_JoystickNumAxes(devices.get(d)); ++a) rest_[size_t(d)].push_back(devices.raw(d, a));
}

bool Learner::moved(const Devices& devices, int& device, int& axis, float& delta, const std::vector<std::pair<int, int>>& taken) const {
  float best = 0.5f;
  bool found = false;
  for (int d = 0; d < devices.count() && d < int(rest_.size()); ++d)
    for (int a = 0; a < int(rest_[size_t(d)].size()); ++a) {
      if (std::find(taken.begin(), taken.end(), std::make_pair(d, a)) != taken.end()) continue;
      const float m = devices.raw(d, a) - rest_[size_t(d)][size_t(a)];
      if (std::fabs(m) > best) best = std::fabs(m), device = d, axis = a, delta = m, found = true;
    }
  return found;
}

bool Learner::hatPressed(const Devices& devices, int& device, int& hat) const {
  for (int d = 0; d < devices.count(); ++d)
    for (int h = 0; h < SDL_JoystickNumHats(devices.get(d)); ++h)
      if (SDL_JoystickGetHat(devices.get(d), h) != SDL_HAT_CENTERED) return device = d, hat = h, true;
  return false;
}

}  // namespace platform::joy
