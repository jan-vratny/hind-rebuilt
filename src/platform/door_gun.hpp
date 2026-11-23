#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The door gun mount: a real gun on a real mount at the player's desk (a joystick with absolute
// axes, the gsgsim project), its axes saying exactly where the gun in the game points. No SDL
// here, so the tests can reach it.
namespace platform::door {

constexpr float kDegree = 3.14159265f / 180.0f;
// The gun's field (Camera::turn, sim::Weapons::doorField).
constexpr float kSide = 69.0f * kDegree, kDown = 69.0f * kDegree, kUp = 29.0f * kDegree;

struct Aim {
  float yaw, pitch;  // radians, + left, + up (as Camera::turn)
};

// The axes (-1..1, x + right, y + up) run stop to stop over the whole field: the middle of y is
// not level, since the mount goes further down than up.
inline Aim aimFromAxes(float x, float y) {
  x = std::clamp(x, -1.0f, 1.0f);
  y = std::clamp(y, -1.0f, 1.0f);
  return {-x * kSide, -kDown + (y + 1.0f) * 0.5f * (kUp + kDown)};
}

// The device by a part of its name: systems put the maker's name before the product's.
inline bool isMount(const std::string& name, const std::string& pattern) {
  return !pattern.empty() && name.find(pattern) != std::string::npos;
}

inline int findMount(const std::vector<std::string>& names, const std::string& pattern) {
  for (size_t i = 0; i < names.size(); ++i)
    if (isMount(names[i], pattern)) return int(i);
  return -1;
}

// The main device (the one flying the helicopter). The mount never is unless named so: plugged
// in alone or first, it would otherwise take the cyclic, and the gun in the player's hands would
// fly the helicopter. `device` counts the other devices.
inline int pickMain(const std::vector<std::string>& names, const std::string& deviceName, int device, const std::string& mountPattern) {
  if (!deviceName.empty())
    for (size_t i = 0; i < names.size(); ++i)
      if (names[i] == deviceName) return int(i);
  std::vector<int> others;
  for (size_t i = 0; i < names.size(); ++i)
    if (!isMount(names[i], mountPattern)) others.push_back(int(i));
  if (others.empty()) return -1;
  return others[size_t(std::clamp(device, 0, int(others.size()) - 1))];
}

// How much of the way to its aim the view moves in dt: the real gun has its weight in the
// player's hands already, the view only smooths the sensor's steps.
inline float follow(float dt) { return 1.0f - std::exp(-dt * 30.0f); }

}  // namespace platform::door
