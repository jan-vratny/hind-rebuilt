#pragma once
#include <vector>

#include <glm/glm.hpp>

#include "render/mesh.hpp"
#include "sim/enemy.hpp"
#include "sim/weapons.hpp"

namespace render {

// Within this distance (m) an interceptor drone in the air is drawn as its model, beyond as a streak.
constexpr float kInterceptorModelRange = 600.0f;

// Tracers, rockets with their smoke trails, dust puffs and explosions, drawn as flat camera-facing
// polygons in the style of the original.
class Effects {
public:
  void impact(const sim::Impact& i);
  // Advances the particles; burning rockets leave smoke.
  void update(float dt, const std::vector<sim::Projectile>& shots);
  // The fires of drones shot down go along with them as they fall: a trail of smoke.
  void follow(const sim::Targets& targets);
  void clear() {
    parts_.clear();
    fires_.clear();
  }
  // Geometry for this frame (world coordinates), seen from `eye`.
  // `shift`: moves the rounds by as much as their helicopter is drawn off its physics position
  // (between two steps), so that they leave the muzzle drawn.
  void build(MeshBuilder& mb, const std::vector<sim::Projectile>& shots, glm::vec3 eye, glm::vec3 shift = glm::vec3(0.0f)) const;

  // The enemy's fire: muzzle flashes, launches, bursts, hits on the helicopter; missiles leave
  // smoke trails; tracers, rockets, missiles and flares to draw.
  void enemy(const sim::EnemyEvent& e);
  void updateEnemy(float dt, const std::vector<sim::EnemyShot>& shots, const std::vector<sim::Flare>& flares);
  // Clouds of chaff: white strips flung out of a little grey cloud.
  void buildChaff(MeshBuilder& mb, const std::vector<sim::Chaff>& chaff, glm::vec3 eye) const;
  void buildEnemy(MeshBuilder& mb, const std::vector<sim::EnemyShot>& shots, const std::vector<sim::Flare>& flares,
                  glm::vec3 eye) const;
  // The runway lights at night (`on` 0..1): points of light, the size of a pixel or two however
  // far, xyz where and w the kind (0 white edge lights, 1 green threshold lights).
  void buildLights(MeshBuilder& mb, const std::vector<glm::vec4>& lights, glm::vec3 eye, float on) const;
  // Hot engines in the dark (`dark` 0..1, as the runway lights): a jet's nozzle (the Geran-3's,
  // an aircraft's) glows a dull orange to the eye; through night vision (an image intensifier,
  // keen on the near infrared) or the drone's camera it shines far off with a halo, a
  // helicopter's two turboshaft exhausts (our Hinds' too, `hinds` their frames) shine bright, and
  // a Shahed's piston engine shows as a faint spot from close by.
  void buildEngineGlows(MeshBuilder& mb, const sim::Targets& targets, const std::vector<glm::mat4>& hinds, glm::vec3 eye,
                        float dark, bool nightVision) const;
  // Smoke (black when burning) pouring from a damaged engine at `pos`.
  void engineSmoke(glm::vec3 pos, glm::vec3 vel, bool burning);

private:
  struct Particle {
    glm::vec3 pos, vel;
    float age, life;
    float size0, size1;  // radius at birth and at the end
    glm::vec4 c0, c1;    // colour and alpha at birth and at the end
    float drag;          // 1/s
  };
  void add(glm::vec3 pos, glm::vec3 vel, float life, float size0, float size1, glm::vec4 c0, glm::vec4 c1,
           float drag = 1.5f);
  float random();

  // A wreck burning out: flames at first, then a column of smoke.
  struct Fire {
    glm::vec3 pos;
    float age = 0, timer = 0;
    int target = -1;  // the wreck burning (Targets::list), carried along while it falls
    bool falling = false;
  };

  std::vector<Particle> parts_;
  std::vector<Fire> fires_;
  float trailTimer_ = 0, enemyTrail_ = 0;
  unsigned rng_ = 777u;
};

}  // namespace render
