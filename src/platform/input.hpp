#pragma once
#include <bitset>
#include <string>

#include <SDL.h>
#include <glm/glm.hpp>

#include "platform/config.hpp"
#include "platform/joystick.hpp"
#include "sim/heli.hpp"

namespace platform {

enum class Action {
  None,
  Gear,
  Engines,       // O: engines to idle, the rotor free (autorotation), or engaged again
  Brakes,        // B: in the air the WSO rescans for another target instead
  Pause,
  Reset,
  Recon,         // R: transmit reconnaissance data
  Doors,         // D: the cargo doors open / shut; with door guns in the air: the gunners free / hold fire
  FlightModel,
  ViewPilot,
  ViewGunner,
  ViewExternal,
  ViewWeapon,     // F7: round the weapon last launched
  ViewTarget,     // F8: from behind its target
  ViewLock,       // F9: past us towards the locked target
  ViewRemote,     // F10: from where we are now
  ViewSpectator,  // F12: from where the camera is now
  ViewWatch,      // F5: round the next unit (Shift: the one before)
  ViewWatchBack,
  ViewDoor,      // F4: at a door gun (again: the other door)
  ViewCenter,
  ZoomIn,
  ZoomOut,
  WeaponSelect,  // E: arm / next weapon
  WeaponSafe,    // Ctrl+E
  Wso,           // S: silicon WSO on/off
  TrackTarget,   // T: track the target along the line of sight
  TimeMax,       // Shift+T: time compression, round x2, x4, x8, x1 (in the gunner's seat: as T)
  TimeNormal,    // Ctrl+T: time compression off
  NextWaypoint,  // W: steer to the next waypoint of the mission
  PrevWaypoint,  // Shift+W: back to the previous one (the original's Shift+N)
  Flares,        // F: the flare launcher on/off
  Chaff,         // C: the chaff launcher on/off
  Jammer,        // K: the IR jammer on/off (the original's works by itself)
  HudColour,     // I: the HUD green or black, as in the original
  InstrumentLights,  // L: the instruments' lamps on/off (seen at night), as in the original
  NightVision,   // V: night vision on/off
  Interceptor,   // G: at a door, send an interceptor drone out and fly it (again: let it go)
  CrewMessage,   // Ctrl+1..0: a message to the others of the network game (messageNumber())
  PilotMessage,  // Ctrl+Shift+1..0: a message to the other pilots
  FormationOrder,  // Shift+1..4: an order to all our helicopters (messageNumber(): 1 home, 2 follow, 3 attack, 4 plan)
  Screenshot,    // F1: the screen to a picture (screenshotDir)
  EndMission,    // Ctrl+Q: "END MISSION? Y/N", as in the original
  Quit,          // Esc
};

// Turns keyboard and joystick state into flight controls. The keyboard layout follows the
// original: arrows = cyclic, Q/A = collective, 1..0 = collective 10..100 %, Z/X = tail rotor.
class Input {
public:
  ~Input();
  void configure(const Config& cfg);
  Action handleEvent(const SDL_Event& e);
  // The screenshot's key pressed (for the screens that have the keyboard to themselves).
  bool screenshotKey(const SDL_Event& e) const { return e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == keys_.screenshot; }
  // The message's key of the last CrewMessage or PilotMessage: 1..10 (0 is 10).
  int messageNumber() const { return messageNumber_; }
  void update(float dt);
  void resetControls();
  // The game paused: the collective's preset keys do nothing (update gets no time then either).
  void setFrozen(bool on) { frozen_ = on; }
  void setCollective(float c) { collectiveTarget_ = controls_.collective = c; }

  const sim::Controls& controls() const { return controls_; }
  // Look modifier (Alt) + arrows, or the joystick's hat (the gamepad's D-pad): view rotation
  // request, -1..1 per axis.
  float viewYaw() const { return viewYaw_; }
  float viewPitch() const { return viewPitch_; }
  // Fire button held (Space or joystick button 0; the left mouse button while the mouse aims).
  bool fire() const { return fire_; }
  // The same but for the key, and the key alone (the lone bowman jumps with it).
  bool fireNoKey() const { return fireNoKey_; }
  bool fireKey() const { return fireKey_; }
  // The mouse aiming a gun (the gunner's sight, a door gun): it is caught in the window, its
  // movement turns the view, its left button fires. Mouse movement since last asked, radians
  // (x + right, y + up).
  void setMouseAim(bool on);
  bool mouseAim() const { return mouseAim_; }
  glm::vec2 takeMouse() { const glm::vec2 m = mouse_; mouse_ = glm::vec2(0.0f); return m; }
  // The right button pressed while the mouse aims, since last asked.
  bool takeRightClick() { const bool r = rightClick_; rightClick_ = false; return r; }
  // The cyclic keys held as the game sees them: 1 forward, 2 back, 4 left, 8 right.
  unsigned cyclicKeys() const { return cyclicKeys_; }
  bool joystickActive() const { return useJoystick_ && main_ >= 0; }
  // The door gun mount. At a door (or flying a drone from one) its trigger and buttons count;
  // doorMount gives where its axes are (x + right, y + up, -1..1), once it has been moved, so
  // that one left on the desk leaves the mouse and the keys their way.
  void setAtDoor(bool on) { atDoor_ = on; }
  bool doorMount(glm::vec2& at) const {
    if (!useJoystick_ || mount_ < 0 || !mountMoved_) return false;
    at = mountAt_;
    return true;
  }
  // Flying an interceptor drone: the joystick's (the gamepad's left) stick steers it instead of
  // the helicopter (droneStick: -1..1, x + right, y + forward), its second and third buttons
  // pick the next target and let the drone go.
  void setStickToDrone(bool on);
  glm::vec2 droneStick() const { return droneStick_; }
  const std::string& deviceName() const { return deviceName_; }

  // The watch view's key (F5) by name, for the hint after a crash.
  std::string watchKeyName() const { return SDL_GetScancodeName(keys_.viewWatch); }

private:
  int messageNumber_ = 1;
  bool frozen_ = false;
  // The devices attached now: the main one chosen, a gamepad opened as one.
  void openDevices();
  void closeDevices();
  float axis(joy::Control c) const;
  float shape(float v) const;
  void recalibrate();

  struct Keys {
    SDL_Scancode cyclicFwd, cyclicBack, cyclicLeft, cyclicRight;
    SDL_Scancode collUp, collDown, collUpAlt, collDownAlt;
    SDL_Scancode pedalLeft, pedalRight;
    SDL_Scancode gear, engines, brakes, pause, reset, recon, doors, flightModel;
    SDL_Scancode viewWeapon, viewTarget, viewLock, viewRemote, viewSpectator, viewWatch;
    SDL_Scancode viewPilot, viewGunner, viewDoor, viewExternal, viewCenter, zoomIn, zoomOut;
    SDL_Scancode fire, weaponSelect, wso, track, nextWaypoint, flares, chaff, jammer, nightVision, hudColour, lights, interceptor, screenshot;
  } keys_{};

  float cyclicRate_ = 1.6f, cyclicReturn_ = 2.5f;
  float pedalRate_ = 2.0f, pedalReturn_ = 3.0f;
  float collectiveRate_ = 0.25f;
  // Q/A move the collective as the original's do: held, it speeds up to collectiveRate_ in
  // collectiveRamp_ s; let go, it runs on and stops in collectiveCoast_ s (a few per cent).
  float collectiveRamp_ = 0.5f, collectiveCoast_ = 0.25f;
  float collectiveSpeed_ = 0.0f;  // per second, + up

  // keyboard part of each control
  float kbCyclicX_ = 0, kbCyclicY_ = 0, kbPedal_ = 0;
  // A pair of opposite keys: with both held the one pressed later counts (so that a key the
  // game missed being let go of cannot cancel the other one).
  struct KeyPair {
    bool plus = false, minus = false;
    int last = 0;
    int resolve(bool p, bool m);
  };
  KeyPair pairX_, pairY_, pairPedal_;
  unsigned cyclicKeys_ = 0;
  // A flying key whose letting go never reached us (the system says nothing is held, SDL still
  // says it is): out of the game till it is pressed or let go of again (X11 only).
  void watchLostKeys(const Uint8* ks, float dt);
  std::bitset<SDL_NUM_SCANCODES> lost_;
  float keyHeldFor_ = 0.0f, keyCheckIn_ = 0.0f;
  int noneHeldSeen_ = 0;
  float collectiveTarget_ = 0;
  sim::Controls controls_;
  float viewYaw_ = 0, viewPitch_ = 0;
  bool fire_ = false, fireNoKey_ = false, fireKey_ = false;
  SDL_Keymod lookMod_ = KMOD_ALT;
  bool mouseEnabled_ = true, mouseAim_ = false, mouseButton_ = false, rightClick_ = false, invertMouse_ = false;
  float mouseSensitivity_ = 0.0025f;  // rad per pixel
  glm::vec2 mouse_{0.0f};
  bool stickToDrone_ = false;
  glm::vec2 droneStick_{0.0f}, heldStick_{0.0f};  // the stick's part of the cyclic, held while flying a drone
  float stickBack_ = 0.0f;  // s left of giving the stick back to the cyclic

  // joystick: every device attached (the stick, a throttle, pedals), the axes they work
  // (platform/joystick.hpp), the main one's buttons, a gamepad's own layout.
  bool joyEnabled_ = true, useJoystick_ = true;
  joy::Map map_;
  joy::Devices devices_;
  int main_ = -1;  // in devices_
  SDL_JoystickID mainId_ = -1;
  int mount_ = -1;  // the door gun mount, in devices_
  SDL_JoystickID mountId_ = -1;
  bool atDoor_ = false, mountMoved_ = false;
  glm::vec2 mountAt_{0.0f}, mountLast_{-2.0f};
  SDL_GameController* pad_ = nullptr;
  std::string deviceName_;
  float deadzone_ = 0.06f, expo_ = 0.3f;
  float center_[joy::kControls] = {};
  float lastThrottle_ = -2.0f;
};

}  // namespace platform
