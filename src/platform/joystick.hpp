#pragma once
#include <string>
#include <vector>

#include <SDL.h>

#include "platform/config.hpp"

namespace platform::joy {

// The controls an axis can work, each from a device of its own (a stick, a throttle quadrant,
// pedals): its value is + to the right, forward, up (the collective), the right pedal.
enum Control { CyclicX, CyclicY, Collective, Pedal, LookX, LookY, kControls };

struct Binding {
  std::string device;  // the device's name (Devices::name), empty: the main device
  int axis = -1;       // -1: none
  bool invert = false;
};

// What hind.ini's [joystick] says: the axes, the hat looking around, the main device (its
// buttons fire, select the weapon and the target; a gamepad's own layout unless mapped).
struct Map {
  Binding axes[kControls];
  Binding hat;  // axis: the hat's number
  int device = 0;           // the main device: by name if attached, else this index
  std::string deviceName;
  bool mapped = false;      // the axes chosen on the preferences page (a gamepad's layout too)
  // The door gun mount (platform/door_gun.hpp): found by this part of its name, its own axes
  // and trigger, used at a door only.
  std::string doorDevice = "GSG Door Gun";
  int doorYawAxis = 0, doorPitchAxis = 1, doorFireButton = 0;
  bool invertDoorYaw = false, invertDoorPitch = false;
};

Map load(const Config& cfg);
// Into the configuration's [joystick] keys.
void save(Config& cfg, const Map& map);
const char* key(Control c);    // "cyclic_x", hind.ini's keys' prefix
const char* label(Control c);  // "Stick left-right"
// The raw axis's way that is + (SDL's stick forward is -).
float sign(Control c);

// Every device attached, open; their names unique (a second one alike gets " #2").
class Devices {
public:
  ~Devices() { close(); }
  // Opens the ones attached since, closes the ones gone.
  void refresh();
  void close();
  int count() const { return int(list_.size()); }
  SDL_Joystick* get(int i) const { return list_[size_t(i)].joy; }
  const std::string& name(int i) const { return list_[size_t(i)].name; }
  SDL_JoystickID id(int i) const { return list_[size_t(i)].id; }
  // Its index for SDL's device functions (SDL_IsGameController), or -1.
  int sdlIndex(int i) const;
  int index(const std::string& name) const;  // -1: not attached
  // The main device of the map, or -1.
  int main(const Map& map) const;
  // The door gun mount, or -1.
  int mount(const Map& map) const;
  // The binding's device, or -1.
  int of(const Map& map, const Binding& b) const;
  // The device's axis, -1..1, raw.
  float raw(int device, int axis) const;
  // The control's value: raw, turned so that + is right, forward, up.
  float value(const Map& map, Control c) const;
  Uint8 hat(const Map& map) const;

private:
  struct Device {
    SDL_Joystick* joy;
    SDL_JoystickID id;
    std::string name;
  };
  std::vector<Device> list_;
};

// Learning the axes: every axis's (and hat's) position when asked, then the one moved most far
// from it, as the control asked for (and which way).
class Learner {
public:
  void start(const Devices& devices);
  // The axis moved more than half its travel since start, not one of `taken`: true, and which.
  bool moved(const Devices& devices, int& device, int& axis, float& delta, const std::vector<std::pair<int, int>>& taken) const;
  bool hatPressed(const Devices& devices, int& device, int& hat) const;

private:
  std::vector<std::vector<float>> rest_;
};

}  // namespace platform::joy
