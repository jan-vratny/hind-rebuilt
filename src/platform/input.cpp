#include "platform/input.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace platform {

namespace {

SDL_Scancode key(const Config& cfg, const char* name) {
  std::string v = cfg.getString("keys", name, "");
  SDL_Scancode sc = SDL_GetScancodeFromName(v.c_str());
  if (sc == SDL_SCANCODE_UNKNOWN) std::fprintf(stderr, "hind.ini: unknown key '%s' for %s\n", v.c_str(), name);
  return sc;
}

// Whether the X server says no key at all is held (false when it cannot say: not on X11). Its own
// connection and libX11 found at run time, so nothing more to build or ship with.
bool x11NoKeysHeld() {
  using Open = void* (*)(const char*);
  using Query = int (*)(void*, char[32]);
  static void* display = nullptr;
  static Query query = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::string(driver) == "x11") {
      if (void* lib = SDL_LoadObject("libX11.so.6")) {
        const Open open = reinterpret_cast<Open>(SDL_LoadFunction(lib, "XOpenDisplay"));
        query = reinterpret_cast<Query>(SDL_LoadFunction(lib, "XQueryKeymap"));
        if (open && query) display = open(nullptr);
      }
    }
  }
  if (!display) return false;
  char keys[32] = {};
  query(display, keys);
  return std::all_of(std::begin(keys), std::end(keys), [](char c) { return c == 0; });
}

// Keyboard axis: holding a key moves the control progressively, releasing returns it to centre.
float stepAxis(float v, bool plus, bool minus, float rate, float ret, float dt) {
  if (plus && !minus) return std::min(1.0f, v + (v < 0.0f ? rate + ret : rate) * dt);
  if (minus && !plus) return std::max(-1.0f, v - (v > 0.0f ? rate + ret : rate) * dt);
  if (v > 0.0f) return std::max(0.0f, v - ret * dt);
  return std::min(0.0f, v + ret * dt);
}

}  // namespace

int Input::KeyPair::resolve(bool p, bool m) {
  if (p && !plus) last = 1;
  if (m && !minus) last = -1;
  plus = p, minus = m;
  if (p && m) return last;
  return p ? 1 : m ? -1 : 0;
}

Input::~Input() { closeDevices(); }

void Input::configure(const Config& cfg) {
  keys_.cyclicFwd = key(cfg, "cyclic_forward");
  keys_.cyclicBack = key(cfg, "cyclic_back");
  keys_.cyclicLeft = key(cfg, "cyclic_left");
  keys_.cyclicRight = key(cfg, "cyclic_right");
  keys_.collUp = key(cfg, "collective_up");
  keys_.collDown = key(cfg, "collective_down");
  keys_.collUpAlt = key(cfg, "collective_up_alt");
  keys_.collDownAlt = key(cfg, "collective_down_alt");
  keys_.pedalLeft = key(cfg, "pedal_left");
  keys_.pedalRight = key(cfg, "pedal_right");
  keys_.gear = key(cfg, "gear");
  keys_.engines = key(cfg, "engines");
  keys_.brakes = key(cfg, "brakes");
  keys_.pause = key(cfg, "pause");
  keys_.reset = key(cfg, "reset");
  keys_.recon = key(cfg, "recon");
  keys_.doors = key(cfg, "doors");
  keys_.flightModel = key(cfg, "flight_model");
  keys_.viewPilot = key(cfg, "view_pilot");
  keys_.viewGunner = key(cfg, "view_gunner");
  keys_.viewDoor = key(cfg, "view_door");
  keys_.viewExternal = key(cfg, "view_external");
  keys_.viewWeapon = key(cfg, "view_weapon");
  keys_.viewTarget = key(cfg, "view_target");
  keys_.viewLock = key(cfg, "view_lock");
  keys_.viewRemote = key(cfg, "view_remote");
  keys_.viewSpectator = key(cfg, "view_spectator");
  keys_.viewWatch = key(cfg, "view_watch");
  keys_.viewCenter = key(cfg, "view_center");
  keys_.zoomIn = key(cfg, "zoom_in");
  keys_.zoomOut = key(cfg, "zoom_out");
  keys_.fire = key(cfg, "fire");
  keys_.weaponSelect = key(cfg, "weapon_select");
  keys_.wso = key(cfg, "silicon_wso");
  keys_.track = key(cfg, "track_target");
  keys_.nextWaypoint = key(cfg, "next_waypoint");
  keys_.flares = key(cfg, "flares");
  keys_.chaff = key(cfg, "chaff");
  keys_.jammer = key(cfg, "jammer");
  keys_.nightVision = key(cfg, "night_vision");
  keys_.hudColour = key(cfg, "hud_colour");
  keys_.lights = key(cfg, "instrument_lights");
  keys_.interceptor = key(cfg, "interceptor");
  keys_.screenshot = key(cfg, "screenshot");
  // An older hind.ini has the jammer on I, now the HUD's colour (as in the original).
  if (keys_.jammer == keys_.hudColour) keys_.jammer = SDL_SCANCODE_K;
  // Held with the cyclic keys it turns the view instead of flying.
  std::string mod = cfg.getString("keys", "look_modifier", "Alt");
  for (char& c : mod) c = char(std::tolower(static_cast<unsigned char>(c)));
  lookMod_ = mod == "ctrl" || mod == "control" ? KMOD_CTRL : mod == "shift" ? KMOD_SHIFT : mod == "gui" || mod == "super" ? KMOD_GUI : KMOD_ALT;
  if (lookMod_ == KMOD_ALT && mod != "alt") std::fprintf(stderr, "hind.ini: look_modifier '%s' unknown, using Alt\n", mod.c_str());

  cyclicRate_ = cfg.getFloat("keyboard", "cyclic_rate", 1.6f);
  cyclicReturn_ = cfg.getFloat("keyboard", "cyclic_return", 2.5f);
  pedalRate_ = cfg.getFloat("keyboard", "pedal_rate", 2.0f);
  pedalReturn_ = cfg.getFloat("keyboard", "pedal_return", 3.0f);
  collectiveRate_ = cfg.getFloat("keyboard", "collective_rate", 0.25f);
  collectiveRamp_ = std::max(0.0f, cfg.getFloat("keyboard", "collective_ramp", 0.5f));
  collectiveCoast_ = std::max(0.0f, cfg.getFloat("keyboard", "collective_coast", 0.25f));

  mouseEnabled_ = cfg.getBool("mouse", "aim", true);
  mouseSensitivity_ = cfg.getFloat("mouse", "sensitivity", 0.0025f);
  invertMouse_ = cfg.getBool("mouse", "invert_y", false);

  joyEnabled_ = useJoystick_ = cfg.getBool("joystick", "enabled", true);
  deadzone_ = cfg.getFloat("joystick", "deadzone", 0.06f);
  expo_ = cfg.getFloat("joystick", "expo", 0.3f);
  map_ = joy::load(cfg);

  int n = SDL_NumJoysticks();
  std::printf("Input devices: %d\n", n);
  for (int i = 0; i < n; ++i) {
    SDL_Joystick* j = SDL_JoystickOpen(i);
    std::printf("  [%d] %s  axes=%d buttons=%d hats=%d%s\n", i, SDL_JoystickNameForIndex(i),
                j ? SDL_JoystickNumAxes(j) : -1, j ? SDL_JoystickNumButtons(j) : -1,
                j ? SDL_JoystickNumHats(j) : -1, SDL_IsGameController(i) ? "  (gamepad mapping)" : "");
    if (j) SDL_JoystickClose(j);
  }
  openDevices();
}

void Input::openDevices() {
  closeDevices();
  if (!joyEnabled_) return;
  devices_.refresh();
  mount_ = devices_.mount(map_);
  mountMoved_ = false;
  mountLast_ = glm::vec2(-2.0f);
  if (mount_ >= 0) {
    mountId_ = devices_.id(mount_);
    std::printf("Door gun mount: %s\n", devices_.name(mount_).c_str());
  }
  main_ = devices_.main(map_);
  if (main_ < 0) return;
  mainId_ = devices_.id(main_);
  deviceName_ = devices_.name(main_);
  // A gamepad has its own layout, until its axes are chosen on the preferences page.
  const int sdl = devices_.sdlIndex(main_);
  if (!map_.mapped && sdl >= 0 && SDL_IsGameController(sdl)) pad_ = SDL_GameControllerOpen(sdl);
  lastThrottle_ = -2.0f;
  for (float& c : center_) c = 0.0f;
  std::printf("Using %s: %s\n", pad_ ? "gamepad" : "joystick", deviceName_.c_str());
  for (int c = 0; c < joy::kControls; ++c) {
    const joy::Binding& b = map_.axes[c];
    const int d = devices_.of(map_, b);
    if (b.axis >= 0 && d >= 0 && !pad_)
      std::printf("  %s: axis %d of %s%s\n", joy::label(joy::Control(c)), b.axis, devices_.name(d).c_str(), b.invert ? ", inverted" : "");
  }
}

void Input::closeDevices() {
  if (pad_) SDL_GameControllerClose(pad_);
  pad_ = nullptr;
  devices_.close();
  main_ = -1;
  mainId_ = -1;
  mount_ = -1;
  mountId_ = -1;
  deviceName_.clear();
}

float Input::axis(joy::Control c) const { return devices_.value(map_, c) - center_[c]; }

float Input::shape(float v) const {
  float a = std::fabs(v);
  if (a < deadzone_) return 0.0f;
  a = (a - deadzone_) / (1.0f - deadzone_);
  a = (1.0f - expo_) * a + expo_ * a * a * a;
  return std::copysign(std::min(a, 1.0f), v);
}

void Input::recalibrate() {
  if (main_ < 0 || pad_) return;
  // The stick, the pedals and the look axes centre where they are now (not the collective).
  for (joy::Control c : {joy::CyclicX, joy::CyclicY, joy::Pedal, joy::LookX, joy::LookY}) center_[c] = devices_.value(map_, c);
  std::printf("Joystick recalibrated (centre %.2f %.2f %.2f)\n", center_[joy::CyclicX], center_[joy::CyclicY], center_[joy::Pedal]);
}

void Input::resetControls() {
  kbCyclicX_ = kbCyclicY_ = kbPedal_ = 0.0f;
  collectiveTarget_ = collectiveSpeed_ = 0.0f;
  controls_ = {};
  lastThrottle_ = -2.0f;
}

void Input::watchLostKeys(const Uint8* ks, float dt) {
  const SDL_Scancode flying[] = {keys_.cyclicFwd, keys_.cyclicBack, keys_.cyclicLeft, keys_.cyclicRight, keys_.pedalLeft, keys_.pedalRight,
                                 keys_.collUp, keys_.collDown, keys_.collUpAlt, keys_.collDownAlt, keys_.fire};
  const auto held = [&](SDL_Scancode s) { return s != SDL_SCANCODE_UNKNOWN && ks[s] && !lost_[s]; };
  if (std::none_of(std::begin(flying), std::end(flying), held)) {
    keyHeldFor_ = 0.0f, noneHeldSeen_ = 0;
    return;
  }
  keyHeldFor_ += dt;
  keyCheckIn_ -= dt;
  if (keyHeldFor_ < 0.3f || keyCheckIn_ > 0.0f || dt <= 0.0f) return;
  keyCheckIn_ = 0.1f;
  // Twice running, as the letting go of a moment ago may only be on its way to us.
  noneHeldSeen_ = x11NoKeysHeld() ? noneHeldSeen_ + 1 : 0;
  if (noneHeldSeen_ < 2) return;
  for (SDL_Scancode s : flying)
    if (held(s)) {
      lost_.set(s);
      std::printf("Key release lost: %s\n", SDL_GetScancodeName(s));
    }
  std::fflush(stdout);
  noneHeldSeen_ = 0;
}

Action Input::handleEvent(const SDL_Event& e) {
  if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && e.key.keysym.scancode < SDL_NUM_SCANCODES) lost_.reset(e.key.keysym.scancode);
  if (e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED) {
    if (e.type == SDL_JOYDEVICEADDED) std::printf("Device connected: %s\n", SDL_JoystickNameForIndex(e.jdevice.which));
    else std::printf("Device disconnected\n");
    openDevices();
    return Action::None;
  }
  // The stick's second and third buttons (a gamepad's B and X): the next weapon (E) and the
  // target along the sight (T), so that the gunner of the network game needs no keyboard; in
  // a drone's camera the next target (B) and letting the drone go (G).
  if (joystickActive() && e.type == SDL_JOYBUTTONDOWN && !pad_ && e.jbutton.which == mainId_) {
    if (e.jbutton.button == 1) return stickToDrone_ ? Action::Brakes : Action::WeaponSelect;
    if (e.jbutton.button == 2) return stickToDrone_ ? Action::Interceptor : Action::TrackTarget;
  }
  if (useJoystick_ && mount_ >= 0 && atDoor_ && e.type == SDL_JOYBUTTONDOWN && e.jbutton.which == mountId_) {
    if (e.jbutton.button == 1) return stickToDrone_ ? Action::Brakes : Action::WeaponSelect;
    if (e.jbutton.button == 2) return stickToDrone_ ? Action::Interceptor : Action::TrackTarget;
  }
  if (joystickActive() && e.type == SDL_CONTROLLERBUTTONDOWN && pad_) {
    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) return stickToDrone_ ? Action::Brakes : Action::WeaponSelect;
    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_X) return stickToDrone_ ? Action::Interceptor : Action::TrackTarget;
  }
  if (mouseAim_ && e.type == SDL_MOUSEMOTION)
    mouse_ += glm::vec2(float(e.motion.xrel), float(invertMouse_ ? e.motion.yrel : -e.motion.yrel)) * mouseSensitivity_;
  if (mouseAim_ && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.button == SDL_BUTTON_LEFT)
    mouseButton_ = e.type == SDL_MOUSEBUTTONDOWN;
  if (mouseAim_ && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) rightClick_ = true;
  // The wheel zooms the gunner's sight.
  if (mouseAim_ && e.type == SDL_MOUSEWHEEL && e.wheel.y != 0) return e.wheel.y > 0 ? Action::ZoomIn : Action::ZoomOut;
  if (e.type != SDL_KEYDOWN || e.key.repeat) return Action::None;

  SDL_Scancode sc = e.key.keysym.scancode;
  Uint16 mod = e.key.keysym.mod;
  bool ctrl = mod & KMOD_CTRL, alt = mod & KMOD_ALT;

  if (ctrl && sc == SDL_SCANCODE_Q) return Action::EndMission;
  // The network game's messages, as the original's two-player game had them.
  if (ctrl && !alt && sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) {
    messageNumber_ = sc - SDL_SCANCODE_1 + 1;
    return mod & KMOD_SHIFT ? Action::PilotMessage : Action::CrewMessage;
  }
  if (sc == SDL_SCANCODE_ESCAPE) return Action::Quit;
  if (sc == keys_.screenshot) return Action::Screenshot;
  if (sc == SDL_SCANCODE_J && ctrl) {
    useJoystick_ = !useJoystick_;
    std::printf("Joystick %s\n", useJoystick_ ? "on" : "off");
    return Action::None;
  }
  if (sc == SDL_SCANCODE_J && alt) {
    recalibrate();
    return Action::None;
  }
  // Enter as in the original game (whatever its manual says), and the key of hind.ini.
  const bool select = sc == keys_.weaponSelect || sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER;
  if (ctrl && select) return Action::WeaponSafe;
  // The reset on the reconnaissance key (R in an older hind.ini) wants Ctrl.
  if (ctrl && sc == keys_.reset) return Action::Reset;
  if (ctrl && sc == keys_.track) return Action::TimeNormal;
  if (ctrl || alt) return Action::None;
  // Orders to the formation, as the original's (Ctrl+1..4 to the wingman, see CrewMessage).
  if ((mod & KMOD_SHIFT) && sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_4) {
    messageNumber_ = sc - SDL_SCANCODE_1 + 1;
    return Action::FormationOrder;
  }

  // Collective presets: 1..9 = 10..90 %, 0 = 100 %.
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) {
    if (frozen_) return Action::None;
    collectiveTarget_ = (sc - SDL_SCANCODE_1 + 1) * 0.1f;
    collectiveSpeed_ = 0.0f;
    return Action::None;
  }
  if (sc == keys_.gear) return Action::Gear;
  if (sc == keys_.engines) return Action::Engines;
  if (sc == keys_.brakes) return Action::Brakes;
  if (sc == keys_.pause) return Action::Pause;
  if (sc == keys_.recon) return Action::Recon;
  if (sc == keys_.doors) return Action::Doors;
  if (sc == keys_.reset) return Action::Reset;
  if (sc == keys_.flightModel) return Action::FlightModel;
  if (sc == keys_.viewPilot) return Action::ViewPilot;
  if (sc == keys_.viewGunner) return Action::ViewGunner;
  if (sc == keys_.viewDoor) return Action::ViewDoor;
  if (sc == keys_.viewExternal) return Action::ViewExternal;
  if (sc == keys_.viewWeapon) return Action::ViewWeapon;
  if (sc == keys_.viewTarget) return Action::ViewTarget;
  if (sc == keys_.viewLock) return Action::ViewLock;
  if (sc == keys_.viewRemote) return Action::ViewRemote;
  if (sc == keys_.viewSpectator) return Action::ViewSpectator;
  if (sc == keys_.viewWatch) return mod & KMOD_SHIFT ? Action::ViewWatchBack : Action::ViewWatch;
  if (sc == keys_.viewCenter) return Action::ViewCenter;
  if (sc == keys_.zoomIn) return Action::ZoomIn;
  if (sc == keys_.zoomOut) return Action::ZoomOut;
  if (select) return Action::WeaponSelect;
  if (sc == keys_.wso) return Action::Wso;
  if (sc == keys_.track) return mod & KMOD_SHIFT ? Action::TimeMax : Action::TrackTarget;
  if (sc == keys_.nextWaypoint) return mod & KMOD_SHIFT ? Action::PrevWaypoint : Action::NextWaypoint;
  if (sc == keys_.flares) return Action::Flares;
  if (sc == keys_.chaff) return Action::Chaff;
  if (sc == keys_.jammer) return Action::Jammer;
  if (sc == keys_.hudColour) return Action::HudColour;
  if (sc == keys_.lights) return Action::InstrumentLights;
  if (sc == keys_.interceptor) return Action::Interceptor;
  if (sc == keys_.nightVision) return Action::NightVision;
  return Action::None;
}

void Input::setMouseAim(bool on) {
  on = on && mouseEnabled_;
  if (on == mouseAim_) return;
  mouseAim_ = on;
  mouseButton_ = false;
  mouse_ = glm::vec2(0.0f);
  SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

void Input::setStickToDrone(bool on) {
  if (on == stickToDrone_) return;
  stickToDrone_ = on;
  stickBack_ = on ? 0.0f : 1.0f;
}

void Input::update(float dt) {
  const Uint8* ks = SDL_GetKeyboardState(nullptr);
  const SDL_Keymod mods = SDL_GetModState();
  bool alt = mods & KMOD_ALT;
  bool ctrl = mods & KMOD_CTRL;
  bool look = mods & lookMod_;
  watchLostKeys(ks, dt);
  auto down = [&](SDL_Scancode s) { return s != SDL_SCANCODE_UNKNOWN && ks[s] && !lost_[s]; };

  // With the look modifier (Alt) held the arrows look around instead of flying.
  bool fwd = !look && down(keys_.cyclicFwd), back = !look && down(keys_.cyclicBack);
  bool left = !look && down(keys_.cyclicLeft), right = !look && down(keys_.cyclicRight);
  cyclicKeys_ = (fwd ? 1u : 0u) | (back ? 2u : 0u) | (left ? 4u : 0u) | (right ? 8u : 0u);
  const int y = pairY_.resolve(fwd, back), x = pairX_.resolve(right, left);
  const int pd = pairPedal_.resolve(down(keys_.pedalRight), down(keys_.pedalLeft));
  kbCyclicY_ = stepAxis(kbCyclicY_, y > 0, y < 0, cyclicRate_, cyclicReturn_, dt);
  kbCyclicX_ = stepAxis(kbCyclicX_, x > 0, x < 0, cyclicRate_, cyclicReturn_, dt);
  kbPedal_ = stepAxis(kbPedal_, pd > 0, pd < 0, pedalRate_, pedalReturn_, dt);
  viewYaw_ = look ? float(down(keys_.cyclicRight)) - float(down(keys_.cyclicLeft)) : 0.0f;
  viewPitch_ = look ? float(down(keys_.cyclicFwd)) - float(down(keys_.cyclicBack)) : 0.0f;

  {
    // Q/A: the lever gathers speed while held and runs on a little when let go.
    const int dir = ctrl || alt ? 0 : int(down(keys_.collUp) || down(keys_.collUpAlt)) - int(down(keys_.collDown) || down(keys_.collDownAlt));
    const float rate = collectiveRate_;
    if (dir != 0) {
      const float accel = collectiveRamp_ > 0.0f ? rate / collectiveRamp_ : 1e9f;
      // Turning round brakes as hard as letting go, then gathers speed the other way.
      const float a = collectiveSpeed_ * float(dir) < 0.0f ? std::max(accel, collectiveCoast_ > 0.0f ? rate / collectiveCoast_ : 1e9f) : accel;
      collectiveSpeed_ = std::clamp(collectiveSpeed_ + float(dir) * a * dt, -rate, rate);
    } else {
      const float decel = collectiveCoast_ > 0.0f ? rate / collectiveCoast_ : 1e9f;
      collectiveSpeed_ = collectiveSpeed_ > 0.0f ? std::max(0.0f, collectiveSpeed_ - decel * dt) : std::min(0.0f, collectiveSpeed_ + decel * dt);
    }
    collectiveTarget_ += collectiveSpeed_ * dt;
    if (collectiveTarget_ <= 0.0f || collectiveTarget_ >= sim::Helicopter::kMaxCollective) collectiveSpeed_ = 0.0f;
  }

  fireKey_ = down(keys_.fire);
  fire_ = mouseAim_ && mouseButton_;
  if (useJoystick_ && mount_ >= 0) {
    const glm::vec2 at(devices_.raw(mount_, map_.doorYawAxis) * (map_.invertDoorYaw ? -1.0f : 1.0f),
                       devices_.raw(mount_, map_.doorPitchAxis) * (map_.invertDoorPitch ? -1.0f : 1.0f));
    // Takes over when it moves (more than a sensor's flicker), as a throttle lever does.
    if (mountLast_.x > -1.5f && (std::fabs(at.x - mountLast_.x) > 0.01f || std::fabs(at.y - mountLast_.y) > 0.01f)) mountMoved_ = true;
    if (mountLast_.x < -1.5f || mountMoved_) mountLast_ = at;
    mountAt_ = at;
    SDL_Joystick* j = devices_.get(mount_);
    if (atDoor_ && map_.doorFireButton < SDL_JoystickNumButtons(j) && SDL_JoystickGetButton(j, map_.doorFireButton)) fire_ = true;
  }
  float jx = 0, jy = 0, jp = 0;
  if (joystickActive()) {
    if (pad_) fire_ = fire_ || SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_A);
    else if (SDL_JoystickNumButtons(devices_.get(main_)) > 0) fire_ = fire_ || SDL_JoystickGetButton(devices_.get(main_), 0);
    // The hat on top of the stick (the D-pad of a gamepad) looks around like Alt + arrows.
    int hat = SDL_HAT_CENTERED;
    if (pad_) {
      auto b = [&](SDL_GameControllerButton k) { return SDL_GameControllerGetButton(pad_, k) != 0; };
      hat = (b(SDL_CONTROLLER_BUTTON_DPAD_UP) ? SDL_HAT_UP : 0) | (b(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? SDL_HAT_DOWN : 0) |
            (b(SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? SDL_HAT_LEFT : 0) | (b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? SDL_HAT_RIGHT : 0);
    } else {
      hat = devices_.hat(map_);
    }
    if (hat != SDL_HAT_CENTERED) {
      viewYaw_ = std::clamp(viewYaw_ + float((hat & SDL_HAT_RIGHT) != 0) - float((hat & SDL_HAT_LEFT) != 0), -1.0f, 1.0f);
      viewPitch_ = std::clamp(viewPitch_ + float((hat & SDL_HAT_UP) != 0) - float((hat & SDL_HAT_DOWN) != 0), -1.0f, 1.0f);
    }
    if (pad_) {
      auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(pad_, a) / 32767.0f; };
      jx = shape(ax(SDL_CONTROLLER_AXIS_LEFTX));
      jy = shape(-ax(SDL_CONTROLLER_AXIS_LEFTY));
      jp = shape(ax(SDL_CONTROLLER_AXIS_RIGHTX));
      // Triggers move the collective like Q/A, the right stick Y nudges it finely.
      float up = ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT), dn = ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
      collectiveTarget_ += (up - dn) * 0.4f * dt;
      collectiveTarget_ += shape(-ax(SDL_CONTROLLER_AXIS_RIGHTY)) * 0.15f * dt;
    } else {
      jx = shape(axis(joy::CyclicX));
      jy = shape(axis(joy::CyclicY));
      jp = shape(axis(joy::Pedal));
      // The look axes (a thumb stick on the throttle) turn the view like the hat, as far as moved.
      if (map_.axes[joy::LookX].axis >= 0 && shape(axis(joy::LookX)) != 0.0f) viewYaw_ = shape(axis(joy::LookX));
      if (map_.axes[joy::LookY].axis >= 0 && shape(axis(joy::LookY)) != 0.0f) viewPitch_ = shape(axis(joy::LookY));
      if (map_.axes[joy::Collective].axis >= 0 && devices_.of(map_, map_.axes[joy::Collective]) >= 0) {
        // An absolute throttle lever takes over only when it moves, so Q/A still work.
        const float t = axis(joy::Collective);
        if (std::fabs(t - lastThrottle_) > 0.01f) {
          if (lastThrottle_ > -1.5f) collectiveTarget_ = (1.0f + t) * 0.5f * sim::Helicopter::kMaxCollective;
          lastThrottle_ = t;
        }
      }
    }
  }

  fireNoKey_ = fire_;
  fire_ = fire_ || fireKey_;

  collectiveTarget_ = std::clamp(collectiveTarget_, 0.0f, sim::Helicopter::kMaxCollective);
  // The lever moves fast but not instantly when a preset key is pressed.
  float diff = collectiveTarget_ - controls_.collective;
  controls_.collective += std::clamp(diff, -1.0f * dt, 1.0f * dt);
  // Flying a drone the stick is its: the helicopter's cyclic stays where the stick left it,
  // and given back it follows the stick again over a second (no jerk where the drone had it).
  droneStick_ = stickToDrone_ ? glm::vec2(jx, jy) : glm::vec2(0.0f);
  if (stickToDrone_) jx = heldStick_.x, jy = heldStick_.y;
  else {
    stickBack_ = std::max(0.0f, stickBack_ - dt);
    jx += (heldStick_.x - jx) * stickBack_, jy += (heldStick_.y - jy) * stickBack_;
    heldStick_ = glm::vec2(jx, jy);
  }
  controls_.cyclicX = std::clamp(kbCyclicX_ + jx, -1.0f, 1.0f);
  controls_.cyclicY = std::clamp(kbCyclicY_ + jy, -1.0f, 1.0f);
  controls_.pedal = std::clamp(kbPedal_ + jp, -1.0f, 1.0f);
}

}  // namespace platform
