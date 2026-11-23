#pragma once
#include <vector>

#include "gamedata/original.hpp"
#include "render/gl.hpp"
#include "sim/debris.hpp"
#include "sim/targets.hpp"
#include "world/terrain.hpp"

namespace render {

// Units on the battlefield, intact and as burnt-out wrecks. The models come from the original
// game when its data is available; otherwise every
// unit is a plain box of its size. Meshes are made the first time a kind is drawn. Other Hinds are
// the original's too (our own simple one without it).
class VehicleRenderer {
public:
  // Returns true when the original models were used for the exercise's vehicles.
  bool build(const gamedata::OriginalData& original);
  // Units within `range` of the eye (soldiers on foot nearer); `time` (s) turns the rotors.
  // livery: our Hinds (the AI wingmen) in the Ukrainian colours (render/livery.hpp).
  // `terrain`: where the aircraft are up in the air, their gear in.
  void draw(const Program& scene, const sim::Targets& targets, glm::vec3 eye, float range, float time, bool livery = false,
            const world::Terrain* terrain = nullptr);
  // The shadows of the aircraft and helicopters within `range` (render/shadow.hpp), the scene
  // shader set up for them (uShadow, the blending): the airframes dark, a flying helicopter's
  // rotor a dithered disc.
  void drawShadows(const Program& scene, const sim::Targets& targets, const world::Terrain& terrain, glm::vec3 eye,
                   float range, glm::vec3 toLight);
  // What a unit breaks into when destroyed (sim::Debris): the model's own parts where it has them
  // (the Mi-17), else cut up by where its faces lie; each about its own centre (model axes). Empty
  // without the original's model, and for a tank, which stays a burnt-out wreck; but a Russian
  // tank's: its turret alone (the ammunition around the crew goes up), its hull staying (`hull`).
  struct Pieces {
    std::vector<GpuMesh> meshes;
    std::vector<glm::vec3> centres;
    std::vector<float> radii;
  };
  const Pieces& pieces(sim::VehicleKind k);
  void drawPieces(const Program& scene, const sim::Debris& debris, sim::VehicleKind k, glm::vec3 eye, float range);

private:
  struct Meshes {
    bool built = false;
    GpuMesh intact, wreck, flying, rotor;  // flying: helicopters in the air, with their rotor apart
    GpuMesh hull;                          // a Russian tank's burnt-out hull, its turret gone
    GpuMesh rotorStopped;                  // the blades standing still, sagging
    glm::vec3 rotorHub{0.0f}, parkedHub{0.0f};  // where the rotor goes on `flying`, on `intact`
    GpuMesh tailRotor;                     // a Hind's, turning in the air, still parked
    glm::vec3 tailHub{0.0f}, parkedTailHub{0.0f};
    float rotorScale = 1.0f;
    bool cull = false;                     // the original's one-sided polygons: back faces culled
    bool piecesBuilt = false;
    Pieces pieces;
  };
  Meshes& meshes(sim::VehicleKind k);

  const gamedata::OriginalData* original_ = nullptr;
  std::vector<Meshes> kinds_;
  GpuMesh disc_;  // the turning rotor's disc, for the shadows
  GpuMesh blur_;  // the turning rotor's disc as seen, faint
};

}  // namespace render
