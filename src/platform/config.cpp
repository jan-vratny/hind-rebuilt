#include "platform/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace platform {

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

struct Default {
  const char* section;
  const char* key;
  const char* value;
};

// Every option with its default. Key names are SDL scancode names ("Up", "Q", "Keypad +", ...).
const Default kDefaults[] = {
    {"video", "width", "1600"},
    {"video", "height", "900"},
    {"video", "fullscreen", "0"},
    {"video", "display", ""},  // empty: system default, N: SDL display number, left / right: outermost monitor
    {"video", "vsync", "1"},
    {"video", "ui_scale", "2"},  // the mission planning screen: window pixels to its pixel at any window size (0: fitted)
    {"video", "msaa", "4"},
    {"video", "fov", "65"},
    {"video", "view_distance", "16000"},
    {"flight", "model", "stable"},
    {"flight", "no_crash", "0"},
    {"keyboard", "cyclic_rate", "1.6"},
    {"keyboard", "cyclic_return", "2.5"},
    {"keyboard", "pedal_rate", "2.0"},
    {"keyboard", "pedal_return", "3.0"},
    {"keyboard", "collective_rate", "0.25"},
    {"keyboard", "collective_ramp", "0.5"},
    {"keyboard", "collective_coast", "0.25"},
    {"keys", "cyclic_forward", "Up"},
    {"keys", "cyclic_back", "Down"},
    {"keys", "cyclic_left", "Left"},
    {"keys", "cyclic_right", "Right"},
    {"keys", "collective_up", "Q"},
    {"keys", "collective_down", "A"},
    {"keys", "collective_up_alt", "="},
    {"keys", "collective_down_alt", "-"},
    {"keys", "pedal_left", "Z"},
    {"keys", "pedal_right", "X"},
    {"keys", "gear", "U"},
    {"keys", "engines", "O"},
    {"keys", "brakes", "B"},
    {"keys", "pause", "P"},
    {"keys", "reset", "Backspace"},  // back to the start (also with Ctrl)
    {"keys", "recon", "R"},      // transmit reconnaissance data, as in the original
    {"keys", "doors", "D"},      // the cargo doors open / shut (troops out and in); with door guns in the air: the gunners free / hold fire
    {"keys", "flight_model", "M"},
    {"keys", "view_pilot", "F2"},
    {"keys", "view_gunner", "F3"},
    {"keys", "view_door", "F4"},  // at a door gun (with door guns); again: the other door
    {"keys", "view_external", "F6"},
    // The original's other external views (its 7, 8, 9, 0, - and =, here on the F keys).
    {"keys", "view_weapon", "F7"},
    {"keys", "view_target", "F8"},
    {"keys", "view_lock", "F9"},
    {"keys", "view_remote", "F10"},
    {"keys", "view_spectator", "F12"},
    {"keys", "view_watch", "F5"},
    {"keys", "view_center", "J"},
    {"keys", "zoom_in", "."},
    {"keys", "zoom_out", ","},
    {"keys", "fire", "Space"},
    {"keys", "weapon_select", "E"},  // besides Enter, as in the original
    {"keys", "silicon_wso", "S"},
    {"keys", "track_target", "T"},
    {"keys", "next_waypoint", "W"},
    {"keys", "flares", "F"},  // the flare launcher on/off
    {"keys", "chaff", "C"},   // the chaff launcher on/off
    {"keys", "jammer", "K"},  // the IR jammer on/off (the original's is always on)
    {"keys", "hud_colour", "I"},         // the HUD green or black, as in the original
    {"keys", "instrument_lights", "L"},  // the instruments' lamps (at night), as in the original
    {"keys", "interceptor", "G"},        // at a door: an interceptor drone out, flown by hand; again: let go
    {"keys", "night_vision", "V"},
    {"keys", "screenshot", "F1"},   // the screen saved as a picture (to [data] screenshots)  // the whole screen in the night vision's teal, as in the original
    {"keys", "look_modifier", "Alt"},  // Alt, Ctrl, Shift or Super: held with the arrows to look around
    {"weapons", "inner_pylons", "b8v20"},
    {"weapons", "outer_pylons", "b8v20"},
    {"weapons", "wingtip_pylons", "9m114"},
    {"weapons", "door_guns", "none"},
    {"audio", "enabled", "1"},  // the original's sounds (SOUND/MAXIMAL.BLK)
    {"audio", "volume", "0.8"},
    {"audio", "voices", "1.0"},  // the voices against the rest
    {"audio", "speech", "1"},    // the briefings read out (from the original's CD)
    {"audio", "music", "1"},     // the menus' music (from the original's CD)
    {"enemy", "fire", "1"},     // the enemy shoots back (in missions)
    {"enemy", "skill", "1.0"},  // 0.5 poor shots, slow to react; 1.5 sharp
    {"data", "original_dir", ""},
    // The original's CD (a directory or its .iso) for the briefings' speech; empty: looked for near it.
    {"data", "cd", ""},
    // Where F1 saves the screenshots; empty: the Pictures folder's Hind (Obrázky/Hind and the like).
    {"data", "screenshots", ""},
    {"world", "zone", "afghan"},  // afghan, kazak, korea, saratov (original game) or procedural
    {"world", "scenery", "1"},    // the war zone's buildings, bridges, copses and runways
    {"world", "hour", "10"},      // local time of the take-off in free flight (missions have their own)
    {"world", "weather", "0"},    // free flight: 0 clear, 1 overcast, 2 light fog, 3 thick fog, 4 rain
    // A mission of the original game (e.g. AFGHNS03), "campaign" for the zone's campaign where
    // the player got to, or "none" for free flight over the exercise area.
    {"world", "mission", "campaign"},
    {"campaign", "afghan", "1"},  // the campaign mission to fly next, per zone
    // The mouse aims the gunner's sight and the door guns (caught in the window in those views).
    {"mouse", "aim", "1"},
    // The door gunners' shot at the nearest drone on the HUD (also the menu's "Gunner hints").
    {"hud", "gunner_hints", "1"},
    {"mouse", "sensitivity", "0.0025"},  // radians a pixel
    {"mouse", "invert_y", "0"},
    {"joystick", "enabled", "1"},
    // The main device (its buttons; SDL's index, or its name when it is attached).
    {"joystick", "device", "0"},
    {"joystick", "device_name", ""},
    {"joystick", "deadzone", "0.06"},
    {"joystick", "expo", "0.3"},
    // Each control's axis (-1: none) and device by name (empty: the main one), set on the
    // preferences page by moving them; mapped: chosen there (a gamepad's own layout otherwise).
    {"joystick", "mapped", "0"},
    {"joystick", "cyclic_x_axis", "0"},
    {"joystick", "cyclic_x_device", ""},
    {"joystick", "invert_cyclic_x", "0"},
    {"joystick", "cyclic_y_axis", "1"},
    {"joystick", "cyclic_y_device", ""},
    {"joystick", "invert_cyclic_y", "0"},
    {"joystick", "collective_axis", "2"},
    {"joystick", "collective_device", ""},
    {"joystick", "invert_collective", "0"},
    {"joystick", "pedal_axis", "3"},
    {"joystick", "pedal_device", ""},
    {"joystick", "invert_pedal", "0"},
    {"joystick", "look_x_axis", "-1"},
    {"joystick", "look_x_device", ""},
    {"joystick", "invert_look_x", "0"},
    {"joystick", "look_y_axis", "-1"},
    {"joystick", "look_y_device", ""},
    {"joystick", "invert_look_y", "0"},
    {"joystick", "look_hat", "0"},
    {"joystick", "look_hat_device", ""},
    {"joystick", "door_device", "GSG Door Gun"},
    {"joystick", "door_yaw_axis", "0"},
    {"joystick", "door_pitch_axis", "1"},
    {"joystick", "door_fire_button", "0"},
    {"joystick", "invert_door_yaw", "0"},
    {"joystick", "invert_door_pitch", "0"},
    // The network game on the local network: the pilot's game listens on this port (--host),
    // the gunner's connects to it (--join ADDRESS).
    {"net", "port", "7024"},
};

}  // namespace

bool Config::load(const std::string& path) {
  std::ifstream f(path);
  if (!f) return false;
  std::string line, section;
  while (std::getline(f, line)) {
    size_t c = line.find_first_of(";#");
    if (c != std::string::npos) line = line.substr(0, c);
    line = trim(line);
    if (line.empty()) continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    data_[section][trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  return true;
}

bool Config::save(const std::string& path) const {
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  std::ofstream f(path);
  if (!f) return false;
  f << "; Hind configuration. Key names are SDL scancode names.\n";
  f << "; flight model: novice | stable | realistic\n";
  f << "; All options are described in README.md (section Configuration).\n";
  for (const auto& [section, kv] : data_) {
    f << "\n[" << section << "]\n";
    for (const auto& [k, v] : kv) f << k << " = " << v << "\n";
  }
  return true;
}

std::string Config::getString(const std::string& s, const std::string& k, const std::string& def) const {
  auto it = data_.find(s);
  if (it == data_.end()) return def;
  auto jt = it->second.find(k);
  return jt == it->second.end() ? def : jt->second;
}

int Config::getInt(const std::string& s, const std::string& k, int def) const {
  std::string v = getString(s, k, "");
  return v.empty() ? def : std::atoi(v.c_str());
}

float Config::getFloat(const std::string& s, const std::string& k, float def) const {
  std::string v = getString(s, k, "");
  return v.empty() ? def : float(std::atof(v.c_str()));
}

bool Config::getBool(const std::string& s, const std::string& k, bool def) const {
  std::string v = getString(s, k, "");
  if (v.empty()) return def;
  return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Config::set(const std::string& s, const std::string& k, const std::string& v) { data_[s][k] = v; }

void Config::applyDefaults() {
  for (const Default& d : kDefaults) {
    auto& sec = data_[d.section];
    if (!sec.count(d.key)) sec[d.key] = d.value;
  }
}

std::string defaultConfigPath() {
  if (std::filesystem::exists("hind.ini")) return "hind.ini";
#ifdef _WIN32
  // Windows: %APPDATA%\hind\hind.ini.
  if (const char* appData = std::getenv("APPDATA"); appData && *appData) return std::string(appData) + "/hind/hind.ini";
  return "hind.ini";
#endif
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  std::string base = xdg && *xdg ? xdg : std::string(home ? home : ".") + "/.config";
  return base + "/hind/hind.ini";
}

std::string defaultScreenshotDir() {
#ifdef _WIN32
  const char* profile = std::getenv("USERPROFILE");
  return std::string(profile && *profile ? profile : ".") + "/Pictures/Hind";
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* homeEnv = std::getenv("HOME");
  const std::string home = homeEnv ? homeEnv : ".";
  std::string pictures = home + "/Pictures";
  // XDG_PICTURES_DIR="$HOME/Obrázky" in user-dirs.dirs.
  std::ifstream dirs((xdg && *xdg ? std::string(xdg) : home + "/.config") + "/user-dirs.dirs");
  for (std::string line; std::getline(dirs, line);) {
    const std::string key = "XDG_PICTURES_DIR=";
    if (line.rfind(key, 0) != 0) continue;
    std::string v = line.substr(key.size());
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    if (v.rfind("$HOME", 0) == 0) v = home + v.substr(5);
    if (!v.empty() && v != home && v != home + "/") pictures = v;
  }
  return pictures + "/Hind";
#endif
}

}  // namespace platform
