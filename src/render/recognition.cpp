#include "render/recognition.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "render/drones.hpp"
#include "render/mesh.hpp"
#include "render/models.hpp"
#include "render/original_mi24.hpp"
#include "render/shaders.hpp"
#include "sim/heli.hpp"

namespace render {

namespace {

// The helicopters' main rotors, m across (the original draws their blades itself, as discs; the
// objects have the hubs only): ours drawn on the hub, scaled to the size.
struct Rotor {
  const char* object;
  float diameter;
};
const Rotor kRotors[] = {{"Mi_24_Hind_Wingman", 17.3f}, {"Mi_17_Hip_H", 21.3f},  {"AH64D_LUX", 14.6f},
                         {"UH_60_Blackhawk", 16.4f},    {"AH_1W_SuperCobra", 14.6f}};

}  // namespace

bool helicopterRotor(const gamedata::OriginalData& data, int index, glm::vec3& at, float& scale) {
  const std::string object = data.name(index);
  for (const Rotor& r : kRotors) {
    if (object != r.object) continue;
    scale = r.diameter / (2.0f * kMainRotorRadius);
    // The sub-object named for the rotor.
    for (const gamedata::OriginalData::Part& part : data.parts(index)) {
      const std::string n = data.name(part.object);
      if (n.find("Rotor") == std::string::npos || n.find("Tail") != std::string::npos) continue;
      const gamedata::Model hub = data.model(part.object);
      at = part.at + glm::vec3((hub.lo.x + hub.hi.x) * 0.5f, hub.hi.y, (hub.lo.z + hub.hi.z) * 0.5f);
      return true;
    }
    // No rotor part (the Apache's): on the top of its mast, the highest point of the front half
    // (behind it the fin stands higher).
    const gamedata::Model model = data.model(index);
    const float half = model.lo.z + 0.5f * (model.hi.z - model.lo.z);
    glm::vec3 top{0.0f, -1e9f, 0.0f};
    for (const gamedata::Polygon& p : model.polys)
      for (const glm::vec3& v : p.pts)
        if (v.y > top.y && std::fabs(v.x) < 0.5f && v.z < half) top = v;
    at = glm::vec3(0.0f, top.y, top.z);
    return true;
  }
  return false;
}

bool helicopterTailRotor(const gamedata::OriginalData& data, int index, glm::vec3& at) {
  bool fin = false;
  for (const gamedata::OriginalData::Part& part : data.parts(index)) {
    const std::string n = data.name(part.object);
    if (n.find("Tail_Rotor") != std::string::npos) return false;  // its own
    if (n.find("Tail_Assembly") == std::string::npos) continue;
    const gamedata::Model m = data.model(part.object);
    const glm::vec3 lo = part.at + m.lo, hi = part.at + m.hi;
    at = {lo.x - 0.08f, lo.y + 0.75f * (hi.y - lo.y), lo.z + 0.7f * (hi.z - lo.z)};
    fin = true;
  }
  return fin;
}

ModelViewer::Entry* ModelViewer::entry(const gamedata::OriginalData& data, const std::string& object) {
  if (auto it = entries_.find(object); it != entries_.end()) return it->second.mesh.empty() ? nullptr : &it->second;
  Entry& e = entries_[object];
  MeshBuilder mb, rotor;
  if (object == "Shahed_136") {
    buildShahed(mb, glm::mat4(1.0f));
  } else if (object == "Geran_3") {
    buildGeran3(mb, glm::mat4(1.0f));
  } else if (object == "Banderol") {
    buildBanderol(mb, glm::mat4(1.0f));
  } else if (object == "Litavr") {
    buildLitavr(mb, glm::mat4(1.0f), 0.6f);
  } else if (object == "Mi_24_Hind_Wingman") {
    // Parked, its gear down.
    MeshBuilder flying;
    buildOriginalWingman(data, mb, flying);
    e.cull = true;
    // Its rotor where the original's main rotor turns on the wingman's Hind.
    buildMainRotor(rotor, glm::translate(glm::mat4(1.0f), kMainRotorHub + glm::vec3(0.0f, sim::Helicopter::kRestHeight, 0.0f)), 0.15f);
  } else if (data.loaded()) {
    int index = data.find(object);
    if (index > 0) {
      index = data.cleanest(index);
      // A poor version of it (Romeo's 10 faces): the most detailed of those by its name.
      if (data.model(index).polys.size() < 40)
        for (int k = 1; k <= data.count(); ++k)
          if (data.name(k) == object && data.model(k).polys.size() > data.model(index).polys.size()) index = k;
      const gamedata::Model model = data.model(index);
      for (const gamedata::Polygon& p : model.polys) mb.face(p.pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
      e.cull = true;
      // A helicopter's blades over its rotor hub.
      glm::vec3 hub;
      float scale;
      if (helicopterRotor(data, index, hub, scale)) buildMainRotor(rotor, glm::scale(glm::translate(glm::mat4(1.0f), hub), glm::vec3(scale)), 0.15f);
    }
  }
  if (mb.verts.empty()) return nullptr;
  glm::vec3 lo(1e9f), hi(-1e9f);
  for (const Vertex& v : mb.verts) lo = glm::min(lo, v.pos), hi = glm::max(hi, v.pos);
  for (const Vertex& v : rotor.verts) lo = glm::min(lo, v.pos), hi = glm::max(hi, v.pos);
  if (!rotor.verts.empty()) e.rotor.upload(rotor.verts);
  e.centre = (lo + hi) * 0.5f;
  e.radius = std::max(0.5f, glm::length(hi - lo) * 0.5f);
  e.mesh.upload(mb.verts);
  return &e;
}

bool ModelViewer::draw(const gamedata::OriginalData& data, const std::string& object, glm::vec4 rect, int windowHeight, float time) {
  if (failed_) return false;
  if (!ready_) {
    if (!prog_.build("recognition", shaders::kSceneVS, shaders::kSceneFS)) {
      failed_ = true;
      return false;
    }
    // The original's textures, as in the flight.
    uploadAtlas(data.textures(), data.textureCount());
    ready_ = true;
  }
  Entry* e = entry(data, object);
  if (!e) return false;
  const int x = int(rect.x), w = int(rect.z - rect.x), h = int(rect.w - rect.y), y = windowHeight - int(rect.w);
  if (w <= 0 || h <= 0) return false;
  glViewport(x, y, w, h);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, w, h);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  if (e->cull) glEnable(GL_CULL_FACE);
  // Seen a little from above, turning; far enough for the whole of it to show.
  const float fov = glm::radians(30.0f), aspect = float(w) / float(h);
  const float fit = std::min(std::tan(fov * 0.5f), std::tan(fov * 0.5f) * aspect);
  const float dist = e->radius / fit;
  const float yaw = 0.8f + time * 0.35f, pitch = glm::radians(15.0f);
  const glm::vec3 eye = e->centre + dist * glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
  const glm::mat4 proj = glm::perspective(fov, aspect, dist * 0.05f, dist * 4.0f);
  prog_.use();
  glUniform1i(prog_.loc("uAtlas"), kAtlasUnit);
  prog_.set("uViewProj", proj * glm::lookAt(eye, e->centre, glm::vec3(0.0f, 1.0f, 0.0f)));
  prog_.set("uModel", glm::mat4(1.0f));
  prog_.set("uSunDir", glm::normalize(glm::vec3(0.5f, 0.8f, 0.35f)));
  prog_.set("uLight", glm::vec3(1.0f));
  prog_.set("uAmbient", 0.45f);
  prog_.set("uDiffuse", 0.65f);
  prog_.set("uFogColor", glm::vec3(0.0f));
  prog_.set("uFogDist", 1.0e9f);
  prog_.set("uCamPos", eye);
  prog_.set("uUnlit", 0.0f);
  prog_.set("uShadow", 0.0f);
  prog_.set("uLivery", 0.0f);
  prog_.set("uLiveryOffset", glm::vec3(0.0f));
  e->mesh.draw();
  glDisable(GL_CULL_FACE);
  if (!e->rotor.empty()) e->rotor.draw();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  return true;
}

}  // namespace render
