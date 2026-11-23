#pragma once
#include <glm/glm.hpp>

#include "world/terrain.hpp"

// Shadows as the original casts them: the aircraft's silhouette laid flat on the ground along
// the sun, dark where the airframe is, a dither of every other pixel where the turning rotor's
// disc is. Drawn with the scene shader (uShadow) multiplying what is under it, each pixel once.
namespace render {

// Where the shadow of a thing at `pos` falls along the light (`toLight`, towards the sun): the
// ground's point and its plane's normal there. False when the sun is too low or the ground too
// far (the shadow would lie kilometres off).
bool shadowPlane(const world::Terrain& terrain, glm::vec3 pos, glm::vec3 toLight, glm::vec3& at, glm::vec3& normal);

// The matrix that flattens the world onto the plane through `at` (normal `normal`) along the
// light; it goes in front of the model's matrix.
glm::mat4 shadowMatrix(glm::vec3 at, glm::vec3 normal, glm::vec3 toLight);

}  // namespace render
