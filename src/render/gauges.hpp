#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "gamedata/picture.hpp"
#include "render/cockpit.hpp"
#include "render/gl.hpp"
#include "world/terrain.hpp"

namespace render {

// Live instruments of the cockpit pictures of the original (COCKPITS/PILOT.LBM, GUNNER.LBM): the
// dial faces are the pictures' own, as the game shows them; this adds what moves. Needles go
// over the picture; the attitude indicators and the moving map, seen through the picture's
// windows, go under it. Positions are pixels of the picture's view ahead (x right of its left
// edge, so the side views are below 0 and above 640; y down).
class PanelGauges {
public:
  PanelGauges() = default;
  PanelGauges(const PanelGauges&) = delete;
  PanelGauges& operator=(const PanelGauges&) = delete;
  ~PanelGauges();
  // Renders the moving map's terrain picture (takes a moment).
  bool init(const world::Terrain& terrain);
  // Renders it again for the area around `centre` (world x, z), such as a mission's.
  void buildMap(const world::Terrain& terrain, glm::vec2 centre);
  // The cockpit's small pictures (COCKPITS/*.640) from the original game directory: lit lamps,
  // counter digits, fonts, the threat warning panel. Whatever is missing is left out.
  void loadParts(const std::string& originalDir, const gamedata::PaletteRange& palette);
  // left, scale: where the picture's view ahead is on screen (screen x = left + px * scale, y from the top);
  // drop: screen pixels the picture has slid down (the view tilted up).
  void drawUnder(const InstrumentData& d, bool gunner, int w, int h, float left, float scale, float drop = 0.0f);
  void drawOver(const InstrumentData& d, bool gunner, int w, int h, float left, float scale, float drop = 0.0f);
  // The original's "END MISSION? Y/N" plate (ENDMISS.640) over the top of the screen, as the
  // original shows it on Ctrl+Q; false when the original's picture is not there.
  bool endMission(int w, int h);

private:
  struct V {
    glm::vec2 p;
    glm::vec4 c;
  };
  glm::vec2 at(glm::vec2 px) const { return {left_ + px.x * scale_, float(h_) - px.y * scale_ - drop_}; }
  void begin(int w, int h, float left, float scale, float drop = 0.0f);
  void flush(glm::vec3 light = glm::vec3(1.0f));
  void tri(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec3 col);  // picture pixels
  void quad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, glm::vec3 col);
  void rect(glm::vec2 a, glm::vec2 b, glm::vec3 col);
  void poly(const std::vector<glm::vec2>& pts, glm::vec3 col);
  void disc(glm::vec2 c, float r, glm::vec3 col, int segs = 32);
  void segment(glm::vec2 a, glm::vec2 b, float width, glm::vec3 col);
  void needle(glm::vec2 c, float angle, float len);
  void text(const std::string& s, glm::vec2 centre, glm::vec3 col);  // centred on a point
  // Rows [row0, row0 + rows) of a picture, or all of it.
  void sprite(const gamedata::Picture& p, glm::vec2 topLeft, int row0 = 0, int rows = -1);
  // A counter of `digits` drum wheels (NUMERICS.640) every `pitch` pixels; the last wheel turns smoothly.
  void drum(float value, int digits, glm::vec2 topLeft, float pitch = 8.0f);
  // Digits of the original's small 3 x 5 font (SMALLNUM.640) in picture pixels, centred on a point.
  void smallText(const std::string& s, glm::vec2 centre, glm::vec3 col);

  // Dials: `c` is the centre, `r` the needle length.
  void gMeter(const InstrumentData& d, glm::vec2 c, float r);
  void clock(const InstrumentData& d, glm::vec2 c, float r);
  void airspeed(const InstrumentData& d, glm::vec2 c, float r);
  void verticalSpeed(const InstrumentData& d, glm::vec2 c, float r);
  void radarAltimeter(const InstrumentData& d, glm::vec2 c, float r);
  void baroAltimeter(const InstrumentData& d, glm::vec2 c, float r);
  void hsi(const InstrumentData& d);
  void pitchYawRoll(const InstrumentData& d);
  void horizon(const InstrumentData& d);
  // An attitude ball's face seen through its round window of radius r: `above` on the side of
  // `up` from the horizon line through h0, `below` on the other.
  void ballFace(glm::vec2 centre, float r, glm::vec2 h0, glm::vec2 up, glm::vec3 above, glm::vec3 below);
  void standbyHorizon(const InstrumentData& d);
  void map(const InstrumentData& d);
  void lamps(const InstrumentData& d);
  void rpmAndFuel(const InstrumentData& d);
  void rwr(const InstrumentData& d);
  void weaponPanel(const InstrumentData& d);

  Program prog_, mapProg_;
  GLuint vao_ = 0, vbo_ = 0, mapVao_ = 0, mapTex_ = 0;
  std::vector<V> verts_;
  float left_ = 0, scale_ = 1, drop_ = 0, alpha_ = 1;
  int w_ = 0, h_ = 0;
  glm::vec2 mapOrigin_{0.0f};  // world x, z of the map texture's corner
  float mapSize_ = 0;          // metres covered by the texture
  glm::vec3 mapOutside_{0.62f, 0.50f, 0.30f};  // the colour beyond it
  glm::vec2 mapCentre_{1e9f};  // world x, z in the middle of the moving map (far away: recentre)
  // A lamp of the threat warning panel lit: its pixels and their lit colours.
  struct LitPart {
    std::vector<glm::vec2> px;
    std::vector<glm::vec3> col;
  };
  void lit(const LitPart& part, glm::vec2 origin);

  gamedata::Picture endMission_;
  gamedata::Picture brakeOn_, gearDown_, gearMoving_, warning_, twiOn_, awarnOn_, liteOn_, numerics_, weapDigits_,
      weapFont_, mapBarH_, mapBarV_, mapBarC_;
  LitPart rwrDir_[10];  // 10, 30, 50, 90 degrees left and right, rear left and right
  std::vector<LitPart> rwrRing_;  // proximity segments in the order they light up
  LitPart rwrClass_[6];
  std::vector<float> digits_;  // 10 glyphs of 3 x 5 coverage values, empty when not loaded
};

}  // namespace render
