#pragma once
#include <vector>

#include <glm/glm.hpp>

#include "gamedata/original.hpp"
#include "render/gl.hpp"
#include "sim/troops.hpp"

// The men on foot about the helicopter (sim::Troops), drawn from the original game's jointed
// figure (Russian_Soldier_AK_47: a body and the limbs hung at the hips, knees, ankles,
// shoulders and elbows), walking with their legs and arms swinging; the casualties on the
// original's stretcher between two men.
namespace render {

class MeshBuilder;

// The original's man `name` into a mesh at `world`, his thighs raised forward by `hip` (rad),
// spread apart by `splay` each and the knees bent back by `knee`, his arms held forward by
// `reach` and bent at the elbows by `elbow`: for the exported model (a door gunner at his gun).
// False without the figure in the data.
bool buildPosedMan(MeshBuilder& mb, const gamedata::OriginalData& data, const char* name, const glm::mat4& world, float hip, float splay,
                   float knee, float reach, float elbow);

class FigureRenderer {
public:
  // False (and nothing drawn) without the original's data.
  bool build(const gamedata::OriginalData& data);
  void draw(const Program& scene, const std::vector<sim::Walker>& walkers) const;

private:
  struct Limb {
    GpuMesh mesh;
    glm::vec3 at{0.0f};  // the joint, in its parent's axes
  };
  struct Man {
    GpuMesh body;
    Limb thigh[2], shin[2], foot[2], arm[2], forearm[2];
    glm::vec3 hand{0.0f};  // the rifle's place, in the right forearm's axes
  };
  bool buildMan(const gamedata::OriginalData& data, const char* name, Man& m);
  // A man at `world` in a walking pose: `swing` the stride's phase, `walk` 0 standing .. 1
  // walking, `tuck` 0..1 legs drawn up (hopping), `reach` arms held forward (+) or back (-), rad.
  void drawMan(const Program& scene, const Man& m, const glm::mat4& world, float swing, float walk, float tuck, float reach,
               bool rifle) const;

  bool ready_ = false;
  Man soldier_, carrier_;
  GpuMesh rifle_, stretcher_;
};

}  // namespace render
