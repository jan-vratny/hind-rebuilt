#pragma once
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "sim/targets.hpp"
#include "sim/weapons.hpp"

#include "sim/enemy.hpp"
#include "sim/heli.hpp"
#include "world/terrain.hpp"

// The network game's hidden role (Ctrl+Shift+R where the seat is chosen): a lone bowman on foot,
// on nobody's side, with a compound bow and arrows that take an explosive warhead, an M60 and an
// M72 LAW, its rocket guided (Enter changes them). He sees only through his own eyes; faster than a man, and hard to kill:
// rounds and blasts near him wear his health down (his own game counts them, from what the
// snapshots carry), and down to nothing he is back at the start a few seconds later. Each warhead
// takes him a while to screw onto its arrow. His game walks him and flies his arrow (with a
// proximity fuse against the Hinds and the units it sees) and his M60's rounds; where they went,
// the host's game decides what they did (arrowBlast, roundOnHind, Targets::damage).
namespace sim {

// A Hind as the arrow's fuse sees it.
struct HindShape {
  glm::vec3 pos{0.0f};
  glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
  int8_t id = -1;  // whose (sim::Weapons::OtherHind: 0 the host's), for what his rounds hit
  bool jammer = false;  // its L-166 IR jammer on (and working)
};

// Anything else on the battlefield as the fuse sees it: its hit box (lo..hi in its axes) and
// the way from the world into them.
struct UnitShape {
  glm::mat4 toLocal{1.0f};
  glm::vec3 lo{0.0f}, hi{0.0f};
  const std::vector<ShapeFace>* faces = nullptr;  // its real shape, when it has one
  int index = -1;  // into Targets::list (-1: a man walking, not a unit)
};

// How far `p` is outside a Hind (m; 0 inside): its fuselage and tail boom, its stub wings, its
// main rotor's disc. `body`: where that is in the Hind's axes.
float hindDistance(glm::vec3 p, const HindShape& h, glm::vec3* body = nullptr);

class Bowman {
public:
  static constexpr float kEye = 1.7f;                // m above his feet
  // m/s: some 30 km/h walking, 100 km/h running (Ctrl); he gets going gradually (m/s^2).
  static constexpr float kWalk = 8.3f, kRun = 27.8f;
  static constexpr float kAccel = 6.0f, kRunAccel = 10.0f, kStop = 20.0f;
  static constexpr float kDrawTime = 2.5f;           // s to draw the bow fully
  static constexpr float kSpeedMin = 30.0f, kSpeedMax = 85.0f;  // m/s, the arrow loosed undrawn .. fully drawn
  static constexpr float kFitTime = 3.0f;           // s to screw the next warhead on (render::buildBowView's turns)
  static constexpr float kArrowLife = 15.0f;         // s in the air at most
  static constexpr float kFuse = 1.0f;               // m: the warhead goes off this near anything
  static constexpr float kJumpSpeed = 3.8f;          // m/s up: some 0.75 m high, over a bridge's railing
  static constexpr float kHurtDrop = 2.0f;           // m: a fall further than this hurts
  static constexpr float kBlastReach = 10.0f;        // m: how far his warhead's blast hurts (arrowBlast, the S-8's)
  static constexpr float kHealth = 100.0f;
  static constexpr float kDeadTime = 5.0f;           // s lying dead before he is back
  // The M60: some 550 rounds a minute at 850 m/s, every fifth a tracer; a belt of 200 with no end
  // of belts, each put in in a few seconds.
  static constexpr float kM60Rate = 9.2f, kM60Speed = 850.0f, kM60Spread = 0.003f, kM60Life = 3.0f;
  static constexpr int kM60Belt = 200;
  static constexpr float kM60Reload = 4.0f;
  // The M72 LAW, for playing more than as it is: its rocket guided. Held on what he aims at (a Hind
  // or a unit within kLawRange he sees, within kLockCone of his look) for kLockTime, it locks on;
  // fired locked, the rocket goes for it on its own (where it will be), else it rides along his
  // look wherever he turns it. Out of the circle (kKeepCone, a little more than kLockCone) the
  // lock is lost. Locked on a Hind its seeker is a heat seeker's, as the Stinger's: the Hind's
  // flares may lure it off (kLawFlare, less from behind), its jammer make it wander off once
  // (kLawJam). It turns no faster than kLawTurn, goes off within kLawFuse of a Hind, on a unit or
  // the ground, or at the end of its flight. A new tube off his back, opened, in kLawReady.
  static constexpr float kLawSpeed = 180.0f, kLawLife = 7.0f, kLawRange = 1200.0f, kLawTurn = 1.6f, kLawFuse = 2.0f;
  static constexpr float kLockCone = 0.06f, kKeepCone = 0.075f, kLockTime = 0.7f;
  static constexpr float kLawFlare = 0.75f, kLawFlareNear = 60.0f, kLawJam = 0.35f;
  // Guided by his look: it follows the look kLawLag (s) behind, and his mouse turns him by
  // kLawMouse of its usual while he does.
  // It turns then no faster than kLawGuidedTurn (rad/s).
  static constexpr float kLawLag = 0.6f, kLawMouse = 0.35f, kLawGuidedTurn = 0.8f;
  static constexpr float kLawReady = 3.0f;
  enum class State : uint8_t { Ready, Drawing, Flying, Fitting };
  enum class Weapon : uint8_t { Bow, M60, Law };
  // Where one of his M60's rounds went: a unit (`target`), a Hind (`hind`), or the ground.
  struct Hit {
    glm::vec3 pos{0.0f};
    int target = -1;
    int8_t hind = -1;
  };

  // Standing at `xz` facing `yaw` (rad, 0 north, clockwise from above), a warhead on his arrow.
  void place(glm::vec2 xz, float yaw, const world::Terrain& terrain);
  // His eyes turned (rad, + left, + up).
  void turn(float yaw, float pitch);
  // Walking (`move` x right, y forward, -1..1), `run` faster; over the ground as it lies, not
  // through the Hinds nor anything else (sliding along it).
  void walk(glm::vec2 move, bool run, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds = {},
            const std::vector<UnitShape>& obstacles = {});
  // Something of these where he would stand at `feet`.
  static bool blocked(glm::vec3 feet, const std::vector<HindShape>& hinds, const std::vector<UnitShape>& obstacles);
  // What he stands on at `xz`, coming from the height `feet`: the ground, or a bridge's deck, a
  // road or a floor no more than a step above his feet.
  static float floorAt(glm::vec2 xz, float feet, const world::Terrain& terrain, const std::vector<UnitShape>& obstacles);
  // Changed his mind: the string let down without loosing, the next draw only after the trigger
  // has been let go of.
  void letDown();
  // The trigger held draws the bow, let go looses the arrow; the arrow flies; the next warhead is
  // screwed on. True when the arrow went off, at `burst`: by a Hind, by anything else (`units`),
  // on the ground, or at the end of its flight.
  bool step(bool trigger, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds,
            const std::vector<UnitShape>& units, glm::vec3& burst);

  // His eyes, bobbing with his steps: down and up once a step, a little to each side in turn
  // (more as he goes faster); lying dead, on the ground.
  glm::vec3 eye() const;
  glm::vec3 look() const;  // where his eyes look, unit
  glm::vec3 up() const;    // his head's up, rocking a little with his steps
  // Where an arrow loosed now, drawn `draw`, comes down on the ground (along its flight, nothing
  // else in its way); false when it would not in its life. `path`: its flight, a point every
  // tenth of a second (and the landing). With `hinds` and `units`: where its fuse would set it
  // off going by one of them (`struck` then true) if it gets there first.
  bool landing(float draw, const world::Terrain& terrain, glm::vec3& at, std::vector<glm::vec3>* path = nullptr,
               const std::vector<HindShape>& hinds = {}, const std::vector<UnitShape>& units = {}, bool* struck = nullptr) const;

  // Enter: the bow, the M60, the LAW and the bow again (the string let down if drawn).
  void change();
  // The M60, each frame: the trigger fires it (every round from his eye along his look, a little
  // spread), its rounds fly and find a Hind, a unit or the ground (`hits`); a belt out, the next
  // put in. Only when it is in his hands it fires; its rounds fly on anyway.
  void stepM60(bool trigger, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds,
               const std::vector<UnitShape>& units, std::vector<Hit>& hits);
  // The LAW, each frame: locking on while it is in his hands and ready; the trigger pressed fires
  // it; its rocket flies (guided) and goes off: `burst` (a Hind's `hind`, a unit's `target` when
  // it struck one). True then. `flares`: the Hinds' flares in the air.
  bool stepLaw(bool trigger, float dt, const world::Terrain& terrain, const std::vector<HindShape>& hinds,
               const std::vector<UnitShape>& units, Hit& burst, const std::vector<Flare>& flares = {});
  // The LAW's rocket.
  struct Rocket {
    bool flying = false;
    glm::vec3 pos{0.0f}, vel{0.0f};
    float age = 0.0f;
    int8_t hind = -1;  // what it goes for (locked when fired): a Hind (HindShape::id), a unit (UnitShape::index)
    int unit = -1;
    glm::vec3 seen{0.0f};  // where that was last
    bool seenOnce = false;
    // Locked on a Hind: the flare it took instead (its id, -1 none), the newest flare it has
    // seen; tried by the jammer; lost (flying straight on).
    int flare = -1, flareSeen = -1;
    bool jamTried = false, lost = false;
    bool decoyed() const { return flare >= 0 || lost; }
    glm::vec3 guide{0.0f, 0.0f, -1.0f};  // unlocked: his look as it follows it (kLawLag behind)
    bool guided() const { return flying && hind < 0 && unit < 0 && !decoyed(); }
  };
  Rocket rocket;
  float lawReady = 0.0f;     // s still to go to the next tube
  // What he aims the LAW at: a Hind or a unit (-1 none), how long so far, locked on; where it is.
  int8_t aimHind = -1;
  int aimUnit = -1;
  float aimFor = 0.0f;
  bool locked = false;
  glm::vec3 aimAt{0.0f};
  // A Hind's or a unit's place (its middle), from what he sees about him; false: not there.
  static bool where(int8_t hind, int unit, const std::vector<HindShape>& hinds, const std::vector<UnitShape>& units, glm::vec3& at);
  // Hurt (points off his health); true when that killed him.
  bool hurt(float points);
  // A round or a blast of the battlefield (from the snapshots): what it takes off his health.
  float damageBy(const Impact& i) const;
  // A round in the air that went from `a` to `b` this frame: how much it took, if it went by
  // close enough to hit him.
  float damageBy(WeaponType w, glm::vec3 a, glm::vec3 b) const;
  // Dead: lying there; true once he may be back (kDeadTime).
  bool dead() const { return health <= 0.0f; }
  bool stepDead(float dt);
  // 0..1 through screwing the warhead on (1 while not at it).
  float fitting() const { return state == State::Fitting ? 1.0f - fit / kFitTime : 1.0f; }

  glm::vec3 pos{0.0f};     // his feet
  float yaw = 0.0f;        // rad, 0 north (-z), clockwise from above (as sim::Walker::heading)
  float pitch = 0.0f;      // rad, + up
  float stride = 0.0f;     // walking cycle, rad
  bool walking = false;
  float bob = 0.0f;        // 0 standing .. 1 walking: how much his steps rock his eyes
  glm::vec2 vel{0.0f};     // m/s over the ground (x east, y south: the world's x and z)
  bool airborne = false;   // jumping or falling
  float pain = 0.0f;       // 1 at the landing of a long fall, gone in a second
  float health = kHealth;
  float blood = 0.0f;      // 1 at a hit, the picture bloodied, gone in a couple of seconds
  float deadFor = 0.0f;    // s lying dead
  Weapon weapon = Weapon::Bow;
  int belt = kM60Belt;     // rounds left in the M60's belt
  float reload = 0.0f;     // s still to go putting in the next belt
  float kick = 0.0f;       // 0..1 the M60 kicking back at a shot (the view)
  // Rounds his M60 has fired (each throws out its case: render::Casings).
  int shots() const { return shot_; }
  std::vector<Projectile> rounds;  // his M60's in the air
  // Up off what he stands on (not in the air).
  void jump();
  // Each frame: his fall, off an edge too, or the floor following under him.
  void fall(float dt, const world::Terrain& terrain, const std::vector<UnitShape>& obstacles);
  State state = State::Ready;
  float draw = 0.0f;       // 0..1 how far the bow is drawn
  float fit = 0.0f;        // s still to go screwing the warhead on
  glm::vec3 arrowPos{0.0f}, arrowVel{0.0f};
  float arrowAge = 0.0f;

private:
  float fireDue_ = 0.0f;   // s to the M60's next round
  uint32_t rng_ = 2463534242u;
  float chance();  // 0..1
  int shot_ = 0;
  bool letDown_ = false;
  bool lawTrigger_ = false;  // the trigger was held (the LAW fires on pressing it)
  float climb_ = 0.0f, fallTop_ = 0.0f;  // m/s up; the highest he was since he left the floor
};

// The host's game: an arrow's warhead went off at `at`; what it did to the Hind `h` (the more the
// nearer, a direct hit taking out the part it struck). True when it hurt it.
// `points`, `reach` and `direct` (m, as a direct hit): the arrow's; a LAW's rocket stronger and
// nearer, its fuse (kLawFuse) a direct hit (lawBlast).
bool arrowBlast(Helicopter& h, glm::vec3 at, float points = 65.0f, float reach = 10.0f, float direct = 1.5f);
inline bool lawBlast(Helicopter& h, glm::vec3 at) { return arrowBlast(h, at, 100.0f, 6.0f, Bowman::kLawFuse + 0.2f); }

}  // namespace sim
