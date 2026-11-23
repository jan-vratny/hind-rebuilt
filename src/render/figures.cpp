#include "render/figures.hpp"

#include <cmath>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "render/mesh.hpp"

namespace render {

namespace {

// The object's own polygons, without its sub-objects.
void upload(GpuMesh& out, const gamedata::OriginalData& data, int object) {
  std::vector<std::string> children;
  for (const gamedata::OriginalData::Part& p : data.parts(object)) children.push_back(data.name(p.object));
  const gamedata::Model m = data.model(object, children);
  MeshBuilder mb;
  for (const gamedata::Polygon& p : m.polys)
    mb.face(p.pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
  out.upload(mb.verts);
}

glm::mat4 pitch(glm::vec3 at, float angle) {
  return glm::rotate(glm::translate(glm::mat4(1.0f), at), angle, glm::vec3(1, 0, 0));
}

// The object's own polygons, without its sub-objects, into a mesh.
void add(MeshBuilder& mb, const gamedata::OriginalData& data, int object, const glm::mat4& at) {
  std::vector<std::string> children;
  for (const gamedata::OriginalData::Part& p : data.parts(object)) children.push_back(data.name(p.object));
  const gamedata::Model m = data.model(object, children);
  for (const gamedata::Polygon& p : m.polys) mb.face(p.pts, p.color, p.oneSided, at, p.uv, p.texture);
}

}  // namespace

bool buildPosedMan(MeshBuilder& mb, const gamedata::OriginalData& data, const char* name, const glm::mat4& world, float hip, float splay,
                   float knee, float reach, float elbow) {
  const int man = data.find(name);
  if (man < 1) return false;
  add(mb, data, man, world);
  // Each limb hangs at its joint.
  for (const gamedata::OriginalData::Part& p : data.parts(man)) {
    const bool arm = data.name(p.object).find("Arm") != std::string::npos;
    const float out = arm ? 0.0f : data.name(p.object).find("Left") != std::string::npos ? splay : -splay;
    const glm::mat4 upper = world * pitch(p.at, 0.0f) * glm::rotate(glm::mat4(1.0f), out, glm::vec3(0, 1, 0)) *
                            glm::rotate(glm::mat4(1.0f), arm ? reach : hip, glm::vec3(1, 0, 0));
    add(mb, data, p.object, upper);
    for (const auto& k : data.parts(p.object)) {
      const glm::mat4 lower = upper * pitch(k.at, arm ? elbow : knee);
      add(mb, data, k.object, lower);
      for (const auto& f : data.parts(k.object))
        if (!arm) add(mb, data, f.object, lower * glm::translate(glm::mat4(1.0f), f.at));
    }
  }
  return true;
}

bool FigureRenderer::buildMan(const gamedata::OriginalData& data, const char* name, Man& m) {
  const int man = data.find(name);
  if (man < 1) return false;
  upload(m.body, data, man);
  // The body's limbs: legs (hips at 0.95 m), arms (shoulders at 1.43 m), left by their x < 0.
  for (const gamedata::OriginalData::Part& p : data.parts(man)) {
    // Left or right by the name: the legs both hang from the middle of the hips.
    const std::string n = data.name(p.object);
    const int side = n.find("Left") != std::string::npos ? 0 : 1;
    if (n.find("Leg") != std::string::npos) {
      m.thigh[side].at = p.at;
      upload(m.thigh[side].mesh, data, p.object);
      for (const auto& k : data.parts(p.object)) {
        m.shin[side].at = k.at;
        upload(m.shin[side].mesh, data, k.object);
        for (const auto& f : data.parts(k.object)) {
          m.foot[side].at = f.at;
          upload(m.foot[side].mesh, data, f.object);
        }
      }
    } else if (n.find("Arm") != std::string::npos) {
      m.arm[side].at = p.at;
      upload(m.arm[side].mesh, data, p.object);
      for (const auto& e : data.parts(p.object)) {
        m.forearm[side].at = e.at;
        upload(m.forearm[side].mesh, data, e.object);
        for (const auto& h : data.parts(e.object)) m.hand = h.at;
      }
    }
  }
  return !m.thigh[0].mesh.empty() && !m.arm[1].mesh.empty();
}

bool FigureRenderer::build(const gamedata::OriginalData& data) {
  ready_ = data.loaded() && buildMan(data, "Russian_Soldier_AK_47", soldier_) && buildMan(data, "Test_Human_Figure", carrier_);
  if (!ready_) return false;
  if (const int ak = data.find("AK_47"); ak > 0) upload(rifle_, data, ak);
  if (const int s = data.find("Casualty_on_Stretcher"); s > 0) upload(stretcher_, data, s);
  return true;
}

void FigureRenderer::drawMan(const Program& scene, const Man& m, const glm::mat4& world, float swing, float walk, float tuck,
                             float reach, bool rifle) const {
  auto put = [&](const glm::mat4& t, const GpuMesh& mesh) {
    scene.set("uModel", t);
    mesh.draw();
  };
  // A step lifts and drops the body a little.
  const glm::mat4 body = world * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.03f * walk * std::fabs(std::cos(swing)), 0.0f));
  put(body, m.body);
  for (int side = 0; side < 2; ++side) {
    // Legs: the thigh swings forward and back, the knee bends as the leg comes through.
    const float phase = swing + (side ? 3.14159f : 0.0f);
    const float hip = 0.45f * walk * std::sin(phase) + 0.6f * tuck;
    const float knee = -0.7f * walk * std::max(0.0f, std::cos(phase)) - 1.1f * tuck;
    const glm::mat4 thigh = body * pitch(m.thigh[side].at, hip);
    put(thigh, m.thigh[side].mesh);
    const glm::mat4 shin = thigh * pitch(m.shin[side].at, knee);
    put(shin, m.shin[side].mesh);
    put(shin * pitch(m.foot[side].at, -knee * 0.3f), m.foot[side].mesh);
    // Arms: swinging against the legs, or held out to the stretcher.
    const float shoulder = reach != 0.0f ? reach : -0.3f * walk * std::sin(phase) * (side && rifle ? 0.3f : 1.0f);
    const float elbow = reach != 0.0f ? 0.3f : (side && rifle ? 0.9f : 0.2f);
    const glm::mat4 arm = body * pitch(m.arm[side].at, shoulder);
    put(arm, m.arm[side].mesh);
    const glm::mat4 forearm = arm * pitch(m.forearm[side].at, elbow);
    put(forearm, m.forearm[side].mesh);
    if (side == 1 && rifle && !rifle_.empty()) put(forearm * glm::translate(glm::mat4(1.0f), m.hand), rifle_);
  }
}

void FigureRenderer::draw(const Program& scene, const std::vector<sim::Walker>& walkers) const {
  if (!ready_ || walkers.empty()) return;
  glEnable(GL_CULL_FACE);  // the original's one-sided polygons
  for (const sim::Walker& w : walkers) {
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), w.pos) * glm::rotate(glm::mat4(1.0f), -w.heading, glm::vec3(0, 1, 0));
    const bool moving = w.wait <= 0.0f && w.goal != sim::Walker::Goal::Stand;
    const float walk = moving && w.hop <= 0.0f ? 1.0f : 0.0f;
    const float tuck = w.hop > 0.0f ? std::sin(w.hop * 3.14159f) : 0.0f;
    if (w.kind == sim::Walker::Kind::Soldier) {
      drawMan(scene, w.soldierModel ? soldier_ : carrier_, world, w.stride, walk, tuck, 0.0f, w.soldierModel);
      continue;
    }
    // Two men, one ahead and one behind, the stretcher between them at the height of their hands.
    drawMan(scene, carrier_, world * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.35f)), w.stride, walk, tuck, -0.25f,
            false);
    drawMan(scene, carrier_, world * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.35f)), w.stride + 3.14159f, walk, tuck,
            0.35f, false);
    scene.set("uModel", world * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.45f, 0.0f)));
    stretcher_.draw();
  }
  glDisable(GL_CULL_FACE);
}

}  // namespace render
