#pragma once
#include <map>
#include <string>

#include <glm/glm.hpp>

#include "gamedata/original.hpp"
#include "render/gl.hpp"

// The recognition library's model: a unit of the original game (an object of 3DOBJS.BIN by name,
// or our own "Shahed_136", "Geran_3", "Banderol", "Litavr"), lit as by day and turning slowly, in a part of the window.
namespace render {

// The original's helicopters have only the hub of their main rotor (the Mi-24's is ours): where
// our blades go on one (object `index`) and how they scale; false: not one of those.
bool helicopterRotor(const gamedata::OriginalData& data, int index, glm::vec3& at, float& scale);
// Where our tail rotor (buildTailRotor, as it is) goes on one of the original's helicopters that
// has none of its own (the Hip): on the left of its fin, high up and to the back.
bool helicopterTailRotor(const gamedata::OriginalData& data, int index, glm::vec3& at);

class ModelViewer {
public:
  // Draws `object` into the window rectangle (drawable pixels, from the top left), turned by
  // `time` (s); false when there is no such model.
  bool draw(const gamedata::OriginalData& data, const std::string& object, glm::vec4 rect, int windowHeight, float time);

private:
  struct Entry {
    GpuMesh mesh;
    GpuMesh rotor;  // our blades, both sides drawn
    glm::vec3 centre{0.0f};
    float radius = 1.0f;
    bool cull = false;
  };
  Entry* entry(const gamedata::OriginalData& data, const std::string& object);
  Program prog_;
  bool ready_ = false, failed_ = false;
  std::map<std::string, Entry> entries_;
};

}  // namespace render
