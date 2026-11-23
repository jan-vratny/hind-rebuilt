#pragma once
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "render/mesh.hpp"

namespace world {
class Terrain;
}

// The lone bowman's (sim::Bowman) compound bow and his arrows.
namespace render {

// An arrow: a black shaft with its nock at the origin, flying to -z, three vanes at the back;
// with its warhead on (buildWarhead at the shaft's front end).
void buildArrow(MeshBuilder& mb, const glm::mat4& m, bool warhead = true);
constexpr float kArrowShaft = 0.8f;  // m, the nock to the warhead's thread

// The grenade warhead: a brass cone stepped in rings, widest at the back (at the origin, where a
// steel collar screws it onto the shaft), a steel point at the front (-z). `spin`: turned that far
// round its axis (rad; a darker seam shows it turning).
void buildWarhead(MeshBuilder& mb, const glm::mat4& m, float spin);

// The compound bow, its grip at the origin, upright (y), shooting along -z, its string drawn
// back `pull` m; returns where the nock point is.
glm::vec3 buildBow(MeshBuilder& mb, const glm::mat4& m, float pull);

// The bowman as the others see him, his feet at the origin, facing -z: bare arms, long black
// hair under a red headband, olive trousers, boots, a quiver of arrows on his back and the
// bow in his left hand; `stride` the walking cycle (rad) and `walk` 0 standing .. 1 walking;
// `draw` 0..1 raising the bow and drawing it at his eye; `pitch` (rad, + up) his aim.
// `weapon` (sim::Bowman::Weapon): 1 the M60 at his hip instead (pointing along his aim), 2 the
// LAW on his right shoulder.
void buildBowmanFigure(MeshBuilder& mb, const glm::mat4& m, float stride, float walk, float draw, float pitch, int weapon = 0);

// The M60 (data/models/m60.mesh: Jawa's, CC BY 4.0; without it one of boxes and rods): the top of
// its pistol grip at the origin, the barrel along -z, the top up (y); its ammunition box under it.
void buildM60(MeshBuilder& mb, const glm::mat4& m);
// Where on it the hands go (its axes): the pistol grip, and under the handguard.
constexpr glm::vec3 kM60Grip{0.0f, -0.045f, 0.02f}, kM60Fore{0.0f, 0.03f, -0.25f};
// What he sees of it, in the camera's axes: held at his right hip, pointing where he looks;
// `kick` 0..1 at a shot, `reload` 0..1 through putting in the next belt (< 0 not).
void buildM60View(MeshBuilder& mb, float kick, float reload);
// Where buildM60View holds it: its axes in the camera's.
glm::mat4 m60View(float kick, float reload);
// The M72 LAW (data/models/m72.mesh when there is one; else one of tubes and plates), opened:
// the middle of its tube's axis under the trigger at the origin, the muzzle along -z, the sights
// up (y); the rear end at +0.45 m, the muzzle at -0.43 m.
void buildLaw(MeshBuilder& mb, const glm::mat4& m);
// Its rocket in flight (data/models/m72rocket.mesh when there is one; else of cones and plates):
// the warhead, the motor tube, the nozzle at its end and the six fins sprung open; the nose at
// the origin, the tail along +z (kLawRocketLength).
void buildLawRocket(MeshBuilder& mb, const glm::mat4& m);
constexpr float kLawRocketLength = 0.55f;
// Where on it the hands go (its axes): over the trigger, under the front tube.
constexpr glm::vec3 kLawGrip{0.0f, 0.05f, -0.03f}, kLawFore{0.0f, -0.04f, -0.3f};
// What he sees of it, in the camera's axes: on his right shoulder, pointing where he looks;
// `kick` 0..1 at the shot, `ready` 0..1 through a new tube coming (it put down, the next one
// brought up; 1: ready).
void buildLawView(MeshBuilder& mb, float kick, float ready);
// Its ejection port (its axes), on the right of the receiver.
constexpr glm::vec3 kM60Port{0.028f, 0.03f, -0.07f};

// The spent cases his M60 throws out, in the world: each from the port out to the right, up and ahead,
// a little different each, tumbling down in an arc, once off the ground; gone in a second and a half.
class Casings {
public:
  // One more: from `at` (world) with the port's `right` and `up` (world), and his own `vel`.
  void eject(glm::vec3 at, glm::vec3 right, glm::vec3 up, glm::vec3 back, glm::vec3 vel);
  void step(float dt, const world::Terrain& terrain);
  // Into `mb` in the camera's axes (`view` the world to them).
  void build(MeshBuilder& mb, const glm::mat4& view) const;
  void clear() { cases_.clear(); }

private:
  struct Case {
    glm::vec3 pos, vel, axis, spinAxis;
    float spin, age;
    bool bounced;
  };
  std::vector<Case> cases_;
  uint32_t rng_ = 0x9e3779b9u;
};

// What he sees of his bow and hands, in the camera's axes (x right, y up, -z ahead): the bow in
// his left hand, `draw` 0..1 drawn, an arrow on its string when `nocked`; screwing a warhead on
// (`fitting` 0..1 through it, < 0 not), the bow down and the arrow across in front of him.
void buildBowView(MeshBuilder& mb, float draw, bool nocked, float fitting);
// How many of the warhead's turns onto its arrow have begun `fitting` (0..1) through screwing it
// on (each one creaks, audio::GameAudio::squeak).
int warheadTurns(float fitting);

}  // namespace render
