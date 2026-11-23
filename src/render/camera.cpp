#include "render/camera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "platform/door_gun.hpp"
#include "render/models.hpp"

namespace render {

void Camera::update(float dt, float yawInput, float pitchInput) {
  const float rate = 1.6f;  // rad/s
  if (inside()) {
    turn(-yawInput * rate * dt, pitchInput * rate * dt);
    if (mode == ViewMode::Door) {
      // The gun follows the aim like a mass on a damped spring: about half a second to swing
      // round and settle, never faster than 2.5 rad/s.
      const int d = seat();
      constexpr float kW = 7.0f, kZeta = 0.85f, kMaxRate = 2.5f;
      auto swing = [&](float& at, float& rate, float aim) {
        rate += (kW * kW * (aim - at) - 2.0f * kZeta * kW * rate) * dt;
        rate = std::clamp(rate, -kMaxRate, kMaxRate);
        at += rate * dt;
      };
      if (doorHeld_[d]) {
        const float f = platform::door::follow(dt);
        lookYaw_[d] += (aimYaw_[d] - lookYaw_[d]) * f;
        lookPitch_[d] += (aimPitch_[d] - lookPitch_[d]) * f;
        rateYaw_[d] = ratePitch_[d] = 0.0f;
      } else {
        swing(lookYaw_[d], rateYaw_[d], aimYaw_[d]);
        swing(lookPitch_[d], ratePitch_[d], aimPitch_[d]);
      }
    }
    // In the modelled cockpit (without the original's pictures, which the view is held to) the
    // pilot's view let go near its default pitch settles back onto it.
    const int i = seat();
    const float off = lookPitch_[i] - kSeatPitch[i];
    if (i == 0 && !levelOnly_[0] && yawInput == 0.0f && pitchInput == 0.0f && std::fabs(off) < glm::radians(12.0f))
      lookPitch_[i] -= std::clamp(off, -0.6f * dt, 0.6f * dt);
  } else {
    if (crash_) yawInput += 0.18f;  // round the wreck in half a minute
    orbitYaw_ = std::fmod(orbitYaw_ + yawInput * rate * dt, 2.0f * glm::pi<float>());
    orbitPitch_ = std::clamp(orbitPitch_ - pitchInput * rate * dt, -0.3f, 1.5f);
  }
}

void Camera::turn(float yaw, float pitch) {
  if (!inside()) return;
  const int i = seat();
  // Magnified, the sight turns as much slower.
  yaw /= magnification(), pitch /= magnification();
  // At a door the gun's field (sim::Weapons::doorField): 70 degrees fore and aft, 70 down, 30 up.
  const bool door = mode == ViewMode::Door;
  const float side = glm::radians(door ? 69.0f : 150.0f), down = glm::radians(door ? 69.0f : 70.0f), up = glm::radians(door ? 29.0f : 60.0f);
  if (i < 2 && levelOnly_[i]) {
    // As far as the cockpit picture goes: left and right, up over the rows painted over it.
    lookYaw_[i] = std::clamp(lookYaw_[i] + yaw, -maxYaw_[i], maxYaw_[i]);
    lookPitch_[i] = std::clamp(lookPitch_[i] + pitch, kSeatPitch[i], kSeatPitch[i] + maxUp_[i]);
    return;
  }
  if (door) {
    // Where the gunner wants the gun; it swings there in update().
    if (yaw != 0.0f || pitch != 0.0f) doorHeld_[i] = false;
    aimYaw_[i] = std::clamp(aimYaw_[i] + yaw, -side, side);
    aimPitch_[i] = std::clamp(aimPitch_[i] + pitch, -down, up);
    return;
  }
  lookYaw_[i] = std::clamp(lookYaw_[i] + yaw, -side, side);
  lookPitch_[i] = std::clamp(lookPitch_[i] + pitch, -down, up);
}

void Camera::setDoorAim(float x, float y) {
  if (mode != ViewMode::Door) return;
  const platform::door::Aim aim = platform::door::aimFromAxes(x, y);
  const int i = seat();
  aimYaw_[i] = aim.yaw;
  aimPitch_[i] = aim.pitch;
  doorHeld_[i] = true;
}

void Camera::center() {
  if (inside()) {
    lookYaw_[seat()] = aimYaw_[seat()] = 0.0f;
    lookPitch_[seat()] = aimPitch_[seat()] = kSeatPitch[seat()];
    rateYaw_[seat()] = ratePitch_[seat()] = 0.0f;
  } else {
    orbitYaw_ = 0.0f;
    orbitPitch_ = 0.22f;
  }
}

void Camera::startCrashOrbit() {
  if (crash_) return;
  crash_ = true;
  before_ = mode;
  distanceBefore_ = distance_;
  mode = ViewMode::External;
  orbitPitch_ = 0.3f;
  distance_ = 55.0f;
}

void Camera::stopCrashOrbit() {
  if (!crash_) return;
  crash_ = false;
  mode = before_;
  distance_ = distanceBefore_;
  orbitPitch_ = 0.22f;
}

void Camera::zoom(float factor) {
  if (mode == ViewMode::Gunner) {
    sight_ = std::clamp(sight_ + (factor < 1.0f ? 1 : -1), 0, 2);
    return;
  }
  // Only the external view itself: from a seat or a door (the mouse wheel there) it would move
  // a camera nobody is looking through.
  if (!orbits()) return;
  distance_ = std::clamp(distance_ * factor, 14.0f, 400.0f);
}

CameraState Camera::compute(const glm::vec3& pos, const glm::quat& q, const world::Terrain& terrain) const {
  CameraState s;
  if (inside()) {
    const float sx = doorSide ? 1.0f : -1.0f;
    s.eye = pos + q * (mode == ViewMode::Door ? kDoorEye * glm::vec3(sx, 1.0f, 1.0f) : mode == ViewMode::Gunner ? kGunnerEye : kPilotEye);
    // From a door, the yaw is from straight out of it.
    const float out = mode == ViewMode::Door ? -sx * glm::half_pi<float>() : 0.0f;
    glm::quat look = q * glm::angleAxis(out + lookYaw(), glm::vec3(0, 1, 0)) * glm::angleAxis(lookPitch(), glm::vec3(1, 0, 0));
    glm::mat4 world = glm::translate(glm::mat4(1.0f), s.eye) * glm::mat4_cast(look);
    s.view = glm::inverse(world);
    return s;
  }
  const glm::vec3 f = q * glm::vec3(0, 0, -1);
  return orbit(pos + glm::vec3(0, 1.0f, 0), std::atan2(f.x, -f.z), terrain);
}

CameraState Camera::orbit(glm::vec3 target, float heading, const world::Terrain& terrain, float scale) const {
  CameraState s;
  const float yaw = -heading + orbitYaw_;
  // Direction from the target to the camera; orbitYaw = 0 is straight behind.
  const glm::vec3 offset{std::sin(yaw) * std::cos(orbitPitch_), std::sin(orbitPitch_), std::cos(yaw) * std::cos(orbitPitch_)};
  s.eye = target + offset * (distance_ * scale);
  s.eye.y = std::max(s.eye.y, terrain.groundHeight(s.eye.x, s.eye.z) + 2.0f);
  s.view = glm::lookAt(s.eye, target, glm::vec3(0, 1, 0));
  return s;
}

CameraState Camera::look(glm::vec3 eye, glm::vec3 at, const world::Terrain& terrain) {
  CameraState s;
  s.eye = eye;
  s.eye.y = std::max(s.eye.y, terrain.groundHeight(s.eye.x, s.eye.z) + 2.0f);
  // Straight up or down lookAt has no 'up': lean it a hair.
  if (glm::length(glm::vec2(at.x - s.eye.x, at.z - s.eye.z)) < 0.01f) at.x += 0.01f;
  s.view = glm::lookAt(s.eye, at, glm::vec3(0, 1, 0));
  return s;
}

}  // namespace render
