#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render/gl.hpp"

namespace render {

struct HudData {
  bool cockpit = true;  // false: external view (text readout instead of the HUD)
  bool gunner = false;  // gunner's seat: helmet sight circle instead of the pilot's HUD
  bool headUp = true;   // the pilot looks ahead through the HUD collimator
  float dim = 0.0f;     // the pause's darkening coming (0 none, 1 the original's ~half brightness)
  bool black = false;   // I: the symbols black instead of green (against a bright sky), as in the original
  float hudShiftX = 0;  // pixels the collimator glass has slid sideways with the cockpit picture
  float hudShiftY = 0;  // ... and up (negative: down, as the pilot looks up)
  float fovY = 1.13f;   // radians, for a conformal horizon line
  float boresightUp = 0.0f;  // how far the aircraft's axis is above the screen centre, pixels
  float speedKmh = 0, altitude = 0, verticalSpeed = 0, torque = 0;
  bool backwards = false;  // flying tail first (the outside view shows the speed negative)
  // With nothing driving the rotor its revolutions (%) take the torque's place.
  bool rotorFree = false;
  float rotorRpm = 0;
  // Ground speed in the aircraft's frame, km/h: + forward / + right (hover drift).
  float forwardKmh = 0, sideKmh = 0;
  bool radarAltitude = false;
  float headingDeg = 0, pitchDeg = 0, rollDeg = 0;
  // Course to the waypoint steered to, degrees (shown in a mission).
  float waypointBearing = 0;
  bool waypointShown = false;
  // Nav mode: the waypoint's letter, km and seconds to it (< 0: not getting there), and the
  // flight path marker: how far the waypoint is off the nose and how much higher it is planned,
  // degrees (the marker on the HUD centre: flying straight to it at the right height).
  char waypointLetter = 0;
  float waypointKm = 0, waypointSeconds = -1;
  glm::vec2 pathMarkerDeg{0.0f};
  float cyclicX = 0, cyclicY = 0, pedal = 0, collective = 0;
  unsigned cyclicKeys = 0;  // cyclic keys held: 1 forward, 2 back, 4 left, 8 right
  std::string flightModel;
  bool gearDown = true, gearMoving = false, brakes = false, joystick = false;
  int timeScale = 1;  // time compression (T): shown while above 1
  int score = -1;     // Quickstart Arcade's score, -1: none
  std::string message;
  float time = 0;
  std::string weapon;          // selected weapon and rounds left, or SAFE
  glm::vec2 turretMark{0.0f};  // gunner: screen position (pixels, y up) the gun points at
  bool turretShown = false;
  float magnification = 1.0f;  // the gunner's sight magnified (shown as "3.3X")
  // At a door gun: its name, side and rounds ("GSHG RIGHT 1432"), and the ring sight where it
  // points (screen position, pixels, y up).
  std::string doorGun;
  glm::vec2 doorMark{0.0f};
  bool doorMarkShown = false;
  // Flying an interceptor drone from a door: only its camera's marks, a cross in the middle, what
  // it is doing (droneInfo), the keys (droneKeys), small corners on each enemy flier it marks
  // (screen pixels, y up) and brackets on the one picked (droneMarks[0] when droneSelected) with
  // its name and range; a box round it once near enough to lock (droneLockable), and a box with
  // a cross through its corners once locked (droneLocked, no stick then).
  struct DroneMark {
    glm::vec2 at;
  };
  // The lone bowman's eyes (sim::Bowman): only a small cross in the middle and what he is doing.
  bool bowView = false, bowCross = true;  // (no cross seen from outside)
  std::string bowInfo;
  float bowHealth = -1.0f;  // 0..1 his health (< 0: not shown)
  std::string bowDead;      // lying dead: what it says in the middle
  // Where the arrow drawn now would come down: the ring round it on the ground, as on the screen
  // (broken where it goes behind the eye).
  std::vector<std::vector<glm::vec2>> bowRing;
  std::vector<std::vector<glm::vec2>> bowPath;  // the arrow's flight there, as on the screen
  bool bowRingHot = false;  // something it can hit in the ring: red, not orange
  // The LAW: what he aims it at, on the screen; 0..1 how far it is to locking on (1 locked;
  // < 0 nothing). Its sight: a circle of its lock cone (px radius; 0 not shown).
  glm::vec2 lawBox{0.0f};
  float lawLock = -1.0f;
  float lawCone = 0.0f;
  // Our Hind down in a network game: the keys to watch the others (bottom middle).
  std::string watchHint;
  bool droneView = false, droneSelected = false, droneLockable = false, droneLocked = false;
  std::string droneInfo, droneKeys, droneMarkText;
  std::vector<DroneMark> droneMarks;
  glm::vec2 droneStick{0.0f};  // where the mouse holds the drone's stick (-1..1, x left, y up)
  // The door gunners' shot at the nearest enemy drone, while one is within a kilometre and a door
  // gun is carried: the door it is in the field of ('L', 'R'; 0 neither), how good the shot is
  // and its parts (0..1; sim::Weapons::doorShot), and what to do about them: its range (m), its
  // speed against ours along our heading (km/h, + it is faster) and up (m/s, + it climbs away),
  // how far it is below us (m); the gunners told to hold their fire.
  bool doorShotShown = false, doorShotHold = false;
  char doorShotSide = 0;
  float doorShot = 0, doorNear = 0, doorMatched = 0, doorBelow = 0, doorOpenFire = 0.3f;
  float doorRange = 0, doorAlongKmh = 0, doorUpMs = 0, doorBelowM = 0;
  // Target tracked by the WSO: e.g. "WSO BMP-2 1250" (range only while the laser has it).
  std::string target;
  glm::vec2 targetMark{0.0f};  // screen position, pixels, y up
  bool targetShown = false, lasing = false;
  // The R-60 selected: the cross-hairs in the box tell its seeker's lock, not the laser's.
  bool r60 = false, r60Lock = false;
  // A drone the Mi-17 called out (sim::Spotter): a diamond on it, in every view.
  glm::vec2 cueMark{0.0f};
  bool cueShown = false;
  // Our other Hinds within reach (the AI wingman, the network game's): a small ring on each.
  std::vector<glm::vec2> friendMarks;
  // Damage (airframe %, nothing when intact) and countermeasures, for the external view.
  int airframe = 100, flares = 0, chaff = 0;
  bool jammer = false;
  // Bombs: where one released now would hit (screen position, pixels, y up).
  glm::vec2 bombMark{0.0f};
  bool bombShown = false;
  // Weapons mode (a weapon armed; otherwise Nav mode): the symbols for aiming the weapon.
  enum class Sight { Gun, Rockets, Bombs, Missile };
  bool weaponsMode = false;
  Sight sight = Sight::Gun;
  std::string weaponLabel;           // e.g. "FAB250:4"
  float minRange = 0, maxRange = 1500;  // m, of the weapon (the range scale)
  float targetRange = -1;            // m, while the laser ranges the target; -1 none
  float countdown = -1;              // s to the computer's bomb release or to the missile's impact
  // Computer-controlled bomb release (silicon WSO with a target): the small bomb-aiming reticle
  // shows how far the target is to the side, in pixels on the HUD.
  bool bombComputer = false;
  float bombLateral = 0;
  // Text for the cockpit picture's text display: screen rectangle x0, y0, x1, y1 (y up).
  std::string displayText;
  glm::vec4 displayRect{0.0f};
  // A page over everything (mission briefing, debriefing): its title, then paragraphs; empty for none.
  std::vector<std::string> page;
};

// Green stroke-drawn head-up display in the style of the original: Nav mode, or Weapons mode
// while a weapon is armed.
class Hud {
public:
  ~Hud();
  bool init();
  void draw(const HudData& d, int width, int height);

private:
  struct V {
    glm::vec2 p;
    glm::vec4 c;
  };
  void line(glm::vec2 a, glm::vec2 b);
  void rect(glm::vec2 a, glm::vec2 b);
  void polyline(std::initializer_list<glm::vec2> pts);
  // align: -1 left, 0 centre, +1 right
  void text(const std::string& s, glm::vec2 pos, float size, int align = -1);
  void drawCockpitHud(const HudData& d, int w, int h);
  void drawGunnerSight(const HudData& d, int w, int h);
  void drawExternalInfo(const HudData& d, int w, int h);
  void drawControls(const HudData& d, int w, int h);
  void drawDoorShot(const HudData& d, int w, int h);
  void drawDroneView(const HudData& d, int w, int h);
  void drawTargetBox(const HudData& d, float s);
  void drawWeaponSymbols(const HudData& d, glm::vec2 centre, float s, float scaleX, float labelY);
  void circle(glm::vec2 c, float r, int n = 24);
  // Drawn in square pixels of `px`, as on the original's 640 x 480 screen: a circle of r pixels
  // (or its arc from 12 o'clock clockwise through `sweep` rad), a straight line.
  void pixelCircle(glm::vec2 c, int r, float px, float sweep = 6.2832f);
  void pixelLine(glm::vec2 a, glm::vec2 b, float px);
  void drawPage(const std::vector<std::string>& page, int w, int h);

  Program prog_;
  GLuint vao_ = 0, vbo_ = 0;
  std::vector<V> verts_;
  glm::vec4 color_{0.3f, 1.0f, 0.4f, 0.9f};
  float thick_ = 1.5f;
};

}  // namespace render
