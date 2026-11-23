#pragma once
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "world/terrain.hpp"

namespace render {

// Door: at a door gun in the cabin, looking out of its window (Camera::doorSide). The original's
// other external views (manual p. 45): Weapon round the weapon last launched, Target from behind
// its target towards it coming, Lock past the helicopter towards the locked target, Remote from
// where the helicopter was, Spectator from where the camera was, Watch round another unit.
// Drone: from the interceptor drone a player at a door flies (G there). Bowman: through the eyes of
// the network game's lone bowman (sim::Bowman).
enum class ViewMode { Pilot, Gunner, External, Door, Weapon, Target, Lock, Remote, Spectator, Watch, Drone, Bowman };

struct CameraState {
  glm::vec3 eye;
  glm::mat4 view;
};

// Views from the pilot's and the gunner's seat (look around with Alt + arrows, each seat keeps its
// own direction) and an external tracking view that orbits the helicopter and follows its heading.
class Camera {
public:
  ViewMode mode = ViewMode::Pilot;
  int doorSide = 1;  // the door of the Door view: 0 left, 1 right
  // Where the door gunner's eye is (body axes, the right door; x mirrored on the left).
  static constexpr glm::vec3 kDoorEye{0.75f, 0.42f, -0.6f};

  void update(float dt, float yawInput, float pitchInput);
  void center();
  // A seat that looks along its cockpit picture (as the original's, left and right) and up: at
  // most `maxYaw` radians either way and `maxUp` up from its default pitch, never down.
  void limitSeat(ViewMode seat, float maxYaw, float maxUp = 0.0f) {
    const int i = seat == ViewMode::Gunner ? 1 : 0;
    maxYaw_[i] = maxYaw, maxUp_[i] = maxUp, levelOnly_[i] = true;
  }
  // Turns the seat view by these angles (radians, + left, + up), within the seat's limits.
  void turn(float yaw, float pitch);
  // The door gun mount (platform/door_gun.hpp) says where the gun is, its axes -1..1 (x + right,
  // y + up), and the view stays close behind it. Mouse or keys turning the gun give it back.
  void setDoorAim(float x, float y);
  float doorAimYaw() const { return aimYaw_[seat()]; }
  float doorAimPitch() const { return aimPitch_[seat()]; }
  // Turns the seat view (radians, + left), e.g. for a screenshot.
  void setLookYaw(float yaw) { lookYaw_[seat()] = aimYaw_[seat()] = yaw; }
  // The seat view tilted this far (radians, + up) from its default pitch, e.g. for a screenshot.
  void tiltLook(float pitch) { lookPitch_[seat()] = aimPitch_[seat()] = kSeatPitch[seat()] + pitch; }
  // The external view nearer or further; in the gunner's seat his sight's magnification up or
  // down (factor < 1: in); nothing in the other seats.
  void zoom(float factor);
  static constexpr float kDefaultOrbitYaw = 0.35f;  // the outside views' start: a little from the side
  float orbitYaw() const { return orbitYaw_; }
  // The external view from this far (m) and from these angles (radians), e.g. for a screenshot.
  void setOrbit(float distance, float yaw, float pitch) { distance_ = distance, orbitYaw_ = yaw, orbitPitch_ = pitch; }
  // The gunner's sight magnifies 1, 3.3 or 10 times (as the Raduga-Sh's two powers); 1 elsewhere.
  float magnification() const { return mode == ViewMode::Gunner ? kSight[sight_] : 1.0f; }
  CameraState compute(const glm::vec3& heliPos, const glm::quat& heliOrient, const world::Terrain& terrain) const;
  bool inside() const { return mode == ViewMode::Pilot || mode == ViewMode::Gunner || mode == ViewMode::Door; }
  // The views that orbit something (rotated with Alt + arrows, zoomed with . and ,; the bowman's
  // only seen from outside, main's bowOrbit).
  bool orbits() const { return mode == ViewMode::External || mode == ViewMode::Weapon || mode == ViewMode::Watch || mode == ViewMode::Bowman; }
  // Round `target` as the external view goes round the helicopter (`heading`: the way it faces).
  // `scale` takes it nearer (a missile is small) or further than the helicopter's view.
  CameraState orbit(glm::vec3 target, float heading, const world::Terrain& terrain, float scale = 1.0f) const;
  // From `eye` towards `at`, kept above the ground.
  static CameraState look(glm::vec3 eye, glm::vec3 at, const world::Terrain& terrain);
  // How far the orbiting views stand off (m).
  float distance() const { return distance_; }
  // Remote and Spectator: where the camera stands.
  glm::vec3 anchor{0.0f};
  // After a crash, as in the original: the external view, the camera circling slowly round the
  // wreck; off again (the seat it was in) when the flight starts over.
  void startCrashOrbit();
  void stopCrashOrbit();
  bool crashOrbit() const { return crash_; }
  // Seat view angles relative to the airframe (radians, pitch negative = looking down).
  float lookYaw() const { return lookYaw_[seat()]; }
  float lookPitch() const { return lookPitch_[seat()]; }
  // The seat view is at its default pitch, where the cockpit picture of the original fits.
  bool lookingLevel() const { return std::fabs(lookPitch() - kSeatPitch[seat()]) < 0.035f; }
  // The seat's default pitch, where the original's cockpit picture fits.
  float seatPitch() const { return kSeatPitch[seat()]; }
  // How much of the cockpit picture is seen: all looking level or up (it slides down, the rows
  // painted over it coming into view), fading out as the view tilts down (gone by 8 degrees),
  // not flickering on and off.
  float pictureShown() const { return std::clamp(1.0f - (kSeatPitch[seat()] - lookPitch() - 0.035f) / 0.105f, 0.0f, 1.0f); }

private:
  int seat() const { return mode == ViewMode::Door ? 2 + doorSide : mode == ViewMode::Gunner ? 1 : 0; }
  // Like the original, both seats look a little down over the instrument panel by default; the
  // angles put the HUD and the sight where the original's cockpit pictures have them.
  // At a door the view is the gun's: straight out of the window by default, a little down.
  static constexpr float kSeatPitch[4] = {-0.205f, -0.108f, -0.12f, -0.12f};
  float lookYaw_[4] = {0.0f, 0.0f, 0.0f, 0.0f}, lookPitch_[4] = {kSeatPitch[0], kSeatPitch[1], kSeatPitch[2], kSeatPitch[3]};
  // At a door the view is along the gun, and the gun is heavy: the stick or the mouse say where
  // the gunner wants it (aim), the gun swings after with its own speed (rate) and comes to rest.
  float aimYaw_[4] = {0.0f, 0.0f, 0.0f, 0.0f}, aimPitch_[4] = {kSeatPitch[0], kSeatPitch[1], kSeatPitch[2], kSeatPitch[3]};
  float rateYaw_[4] = {}, ratePitch_[4] = {};
  bool doorHeld_[4] = {};  // the real gun on its mount has it: no make-believe inertia on top
  float maxYaw_[2] = {2.618f, 2.618f};  // 150 degrees
  float maxUp_[2] = {0.0f, 0.0f};
  bool levelOnly_[2] = {false, false};
  float orbitYaw_ = kDefaultOrbitYaw, orbitPitch_ = 0.22f, distance_ = 32.0f;
  bool crash_ = false;
  static constexpr float kSight[3] = {1.0f, 3.3f, 10.0f};
  int sight_ = 0;
  ViewMode before_ = ViewMode::Pilot;
  float distanceBefore_ = 32.0f;
};

}  // namespace render
