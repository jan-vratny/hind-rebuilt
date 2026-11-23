#include "menu/planner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gamedata/original.hpp"
#include "gamedata/picture.hpp"

namespace menu {

namespace {

// The map fills the screen (the tool box stands on it); fitting the route aims at the part left
// of the tool box.
// (As the original's 640 x 480; on a larger screen the tool box stays at the right edge and
// Exit in the bottom right corner, the map shows more.)
const glm::vec2 kMainLo{0.0f, 0.0f};
constexpr float kToolBoxX = 557.0f;  // on the 640-pixel screen; toolX() on this one

// The original's interface colours (UIDATA: CLOSEOFF.BM and the other boxes use palette indices
// 0xf2 .. 0xf7): darkest, dark, the tool box's ground, the face, light, white.
glm::vec4 rgb(int r, int g, int b) { return {float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f, 1.0f}; }
const glm::vec4 kGui2 = rgb(97, 32, 32), kGui3 = rgb(113, 48, 48), kGui4 = rgb(113, 56, 56), kGui5 = rgb(142, 56, 56),
                kGui6 = rgb(190, 77, 77), kWhite = rgb(255, 255, 255);

const glm::vec4 kPanelDark{0.05f, 0.05f, 0.05f, 1.0f};
const glm::vec4 kText{0.82f, 0.84f, 0.80f, 1.0f};
const glm::vec4 kAmber{1.0f, 0.74f, 0.25f, 1.0f};
const glm::vec4 kRoute{1.0f, 0.95f, 0.35f, 1.0f};
const glm::vec4 kEnemy{0.95f, 0.22f, 0.15f, 1.0f};
const glm::vec4 kOurs{0.30f, 0.55f, 1.0f, 1.0f};

// The tool box, measured on the original (pixels lo .. hi - 1).
enum ButtonId { kBriefing, kTakeOff, kZoom, kRotate, kWay, kDisable, kBack, kFit, kKey, kMet, k3D, kPay, kPoint, kTask,
                kSave, kRestore, kExit, kDebrief };
const glm::vec2 kPanelLo{557.0f, 40.0f}, kPanelHi{640.0f, 202.0f};
constexpr float kToolX[2] = {559.0f, 598.0f}, kToolW[2] = {38.0f, 39.0f};
constexpr float kToolY[7] = {57.0f, 77.0f, 101.0f, 121.0f, 141.0f, 161.0f, 181.0f};
constexpr float kToolH = 19.0f;
// The waypoints' window: Ins, Del, Info (measured on the original).
const char* kWayButton[3] = {"Ins", "Del", "Info"};
const glm::vec2 kWayButtonLo[3] = {{22.0f, 2.0f}, {62.0f, 2.0f}, {102.0f, 2.0f}};
const glm::vec2 kWayButtonHi[3] = {{61.0f, 21.0f}, {101.0f, 21.0f}, {141.0f, 21.0f}};

// The windows of the pictures (briefing, payload): their inside on the screen.
const glm::vec2 kPictureLo{7.0f, 7.0f}, kPictureHi{551.0f, 410.0f};
// The payload window's bar for what is chosen (in the picture's coordinates); the pylon menu.
const glm::vec2 kBarLo{173.0f, 378.0f}, kBarHi{379.0f, 399.0f};
constexpr float kMenuW = 69.0f, kMenuItemH = 20.0f;
constexpr float kKnot = 0.514444f;  // m/s

bool inside(glm::vec2 p, glm::vec2 lo, glm::vec2 hi) { return p.x >= lo.x && p.y >= lo.y && p.x < hi.x && p.y < hi.y; }

// Payload picture (PAYLOAD.LBM): where the stores' pictures go on the pylons (left, right) and
// what can be clicked. Measured on the picture.
struct Spot {
  glm::vec2 lo, hi;
};
const Spot kPylonSpot[sim::kPylons] = {{{176, 168}, {218, 232}}, {{146, 176}, {176, 236}}, {{100, 192}, {146, 228}}};
// Mirror image of a spot on the left half: the picture is symmetric about x = 272.
Spot mirrored(const Spot& s) { return {{544.0f - s.hi.x, s.lo.y}, {544.0f - s.lo.x, s.hi.y}}; }

// The stores laid out on the ground, left half (each has its twin on the right).
struct GroundStore {
  sim::Store store;
  Spot spot;
};
const GroundStore kGround[] = {
    {sim::Store::Shturm, {{16, 256}, {68, 286}}},  {sim::Store::UB32, {{74, 248}, {112, 288}}},
    {sim::Store::B8V20, {{113, 251}, {150, 292}}},  {sim::Store::Kmgu, {{150, 256}, {186, 300}}},
    {sim::Store::Upk23, {{198, 260}, {230, 302}}}, {sim::Store::R60, {{30, 286}, {80, 330}}},
    {sim::Store::Fab250, {{80, 303}, {112, 340}}}, {sim::Store::Ofab250, {{126, 308}, {160, 349}}},
    {sim::Store::Fab500, {{180, 303}, {220, 352}}}, {sim::Store::Zab500, {{226, 320}, {260, 362}}},
    // Ours, drawn here (interceptorPicture, at kInterceptorGroundAt).
    {sim::Store::Interceptor, {{116, 352}, {171, 392}}},
};

// The pictures of the stores on the pylons (MENUSCRN), by store and pylon: name for the left
// side and the right one, and where they go.
struct Sprite {
  sim::Store store;
  sim::Pylon pylon;
  const char* left;
  const char* right;
  glm::vec2 at, atRight;
};
const Sprite kSprites[] = {
    {sim::Store::B8V20, sim::Pylon::Inner, "B-8_LI.LBM", "B-8_RI.LBM", {182, 195}, {343, 195}},
    {sim::Store::B8V20, sim::Pylon::Outer, "B-8_LO.LBM", "B-8_RO.LBM", {154, 201}, {370, 201}},
    {sim::Store::UB32, sim::Pylon::Inner, "UB32A_LI.LBM", "UB32A_RI.LBM", {183, 195}, {345, 195}},
    {sim::Store::Upk23, sim::Pylon::Inner, "UPK23_LI.LBM", "UPK23_RI.LBM", {187, 194}, {347, 194}},
    {sim::Store::Fab250, sim::Pylon::Inner, "FB250_LI.LBM", "FB250_RI.LBM", {187, 195}, {348, 195}},
    {sim::Store::Fab250, sim::Pylon::Outer, "FB250_LO.LBM", "FB250_RO.LBM", {159, 201}, {376, 201}},
    {sim::Store::Fab500, sim::Pylon::Inner, "FAB500_L.LBM", "FAB500_R.LBM", {183, 195}, {345, 194}},
    {sim::Store::Ofab250, sim::Pylon::Inner, "OFAB_LI.LBM", "OFAB_RI.LBM", {186, 195}, {348, 195}},
    {sim::Store::Ofab250, sim::Pylon::Outer, "OFAB_LO.LBM", "OFAB_RO.LBM", {158, 201}, {375, 201}},
    {sim::Store::Zab500, sim::Pylon::Inner, "FAE_LI.LBM", "FAE_RI.LBM", {186, 195}, {346, 195}},
    {sim::Store::Kmgu, sim::Pylon::Inner, "KMGU_LI.LBM", "KMGU_RI.LBM", {183, 194}, {345, 194}},
    {sim::Store::R60, sim::Pylon::Outer, "R-60_LO.LBM", "R-60_RO.LBM", {151, 199}, {373, 199}},
    {sim::Store::Shturm, sim::Pylon::Outer, "9M114_LO.BBM", "9M114_RO.LBM", {149, 189}, {366, 189}},
    {sim::Store::Shturm, sim::Pylon::Wingtip, "9M114_LW.LBM", "9M114_RW.LBM", {108, 207}, {404, 207}},
};

// The door gun (tools/gshg2sprite.py): where it lies on the concrete and where it is put out of
// the cabin's window (left side, the right ones mirrored), what can be clicked.
#include "menu/gshg_sprite.inc"
// (The picture is not quite symmetric about the helicopter: the right side of the fuselage is
// further out.)
const glm::vec2 kGshgGroundAt{0.0f, 330.0f}, kGshgWindowAt[2] = {{200.0f, 150.0f}, {315.0f, 150.0f}};
const Spot kGshgSpot{{4, 336}, {104, 372}};
const Spot kDoorSpot{{219, 136}, {242, 178}};
const char* kDoorName[3] = {"Left Door", "Right Door", "Both Doors"};

// An RGBA picture, or its mirror image.
gamedata::Picture rgbaPicture(const unsigned char* px, int w, int h, bool mirror) {
  gamedata::Picture p;
  p.width = w, p.height = h;
  p.rgba.resize(size_t(w) * size_t(h) * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int k = 0; k < 4; ++k) p.rgba[(size_t(y) * w + x) * 4 + k] = px[(size_t(y) * w + (mirror ? w - 1 - x : x)) * 4 + k];
  return p;
}

// The interceptor drones' rack (not the original's: drawn here), seen from the front as the
// stores on the concrete are: its frame with two tiers of three Litavrs, each a dark
// round body with four arms in an X out to `r`, a nacelle at the end of each and its pusher's
// disc round it, `gap` pixels apart side by side and `rowGap` between the tiers.
gamedata::Picture interceptorPicture(int r, int gap, int rowGap) {
  const int cell = 2 * r + gap, cellY = 2 * r + rowGap, margin = gap / 2 + 1, w = 3 * cell + 2 * margin, h = 2 * cellY + 2 * margin;
  gamedata::Picture p;
  p.width = w, p.height = h;
  p.rgba.assign(size_t(w) * size_t(h) * 4, 0);
  auto set = [&](int x, int y, glm::u8vec3 c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    unsigned char* q = &p.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
    q[0] = c.r, q[1] = c.g, q[2] = c.b, q[3] = 255;
  };
  const glm::u8vec3 frame{112, 114, 108}, dark{38, 40, 38}, lit{96, 98, 94}, arm{62, 64, 60}, red{120, 34, 30}, prop{150, 152, 146};
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (x == 0 || y == 0 || x == w - 1 || y == h - 1) set(x, y, frame);
  for (int row = 0; row < 2; ++row) {
    const int cy = margin + row * cellY + cellY / 2;
    for (int x = 0; x < w; ++x) set(x, cy, frame);
    for (int col = 0; col < 3; ++col) {
      const int cx = margin + col * cell + cell / 2, n = std::max(1, r / 5), body = std::max(1, r / 4), t = r / 8, disc = r * 2 / 3;
      // A filled round spot, lit from the top left.
      auto spot = [&](int x0, int y0, int rad) {
        for (int y = -rad; y <= rad; ++y)
          for (int x = -rad; x <= rad; ++x)
            if (x * x + y * y <= rad * rad + rad) set(x0 + x, y0 + y, x + y < -rad / 2 ? lit : dark);
      };
      for (int sx : {-r, r})
        for (int sy : {-r, r})
          if (disc > n + 1)
            for (int k = 0; k < 8 * disc; ++k) {
              const float a = 6.2832f * float(k) / float(8 * disc);
              set(cx + sx + int(std::lround(disc * std::cos(a))), cy + sy + int(std::lround(disc * std::sin(a))), prop);
            }
      for (int k = -r; k <= r; ++k)
        for (int d = -t; d <= t; ++d) {
          const glm::u8vec3 c = std::abs(std::abs(k) - r / 2) <= t ? red : arm;
          set(cx + k + d, cy + k, c), set(cx + k + d, cy - k, c);
        }
      for (int sx : {-r, r})
        for (int sy : {-r, r}) spot(cx + sx, cy + sy, n);
      spot(cx, cy, body);
    }
  }
  return p;
}
// Where it lies on the concrete (left; the right one mirrored) and hangs under the wing between
// the pylons: right under the outer one's end, on a strut from the inner one's (higher: x, y). The one on
// the pylons a little under the Hind's scale (its 6.5 m span ~300 pixels would make the 1.6 m
// rack ~70), the one on the concrete a bit nearer.
// The picture is not quite symmetric: the right wing's pylons are 4 pixels further out than the
// left's mirrored.
const glm::vec2 kInterceptorGroundAt{116.0f, 352.0f}, kInterceptorPylonAt[2] = {{160.0f, 201.0f}, {348.0f, 201.0f}};
const glm::vec4 kInterceptorStrut{112 / 255.0f, 114 / 255.0f, 108 / 255.0f, 1.0f};
const glm::vec2 kInterceptorStruts[2] = {{193.0f, 194.0f}, {354.0f, 194.0f}};

const char* kWaypointType[] = {"not used", "Departure", "Turning point", "Landing", "Target"};

template <typename... A>
std::string fmt(const char* f, A... a) {
  char buf[160];
  std::snprintf(buf, sizeof buf, f, a...);
  return buf;
}

}  // namespace

bool Planner::init(const std::string& originalDir, const std::string& zone) {
  auto file = [&](const std::string& rel) { return originalDir.empty() ? std::string() : gamedata::findOriginalFile(originalDir, rel); };
  if (!canvas_.init(file("FONTS/11X15.LBM"))) return false;
  if (!originalDir.empty()) canvas_.setBackdrop(file("MENUSCRN/TWO_PLYR.LBM"));
  auto load = [&](const std::string& rel, render::Texture& t, int transparent) {
    gamedata::Picture pic;
    std::string error;
    const std::string path = file(rel);
    if (!path.empty() && gamedata::loadPbm(path, pic, transparent, error)) t.upload(pic);
    else if (!originalDir.empty()) std::fprintf(stderr, "%s: %s\n", rel.c_str(), path.empty() ? "not found" : error.c_str());
  };
  const std::string zoneDir = zone == "kazak" ? "KAZAK" : zone == "korea" ? "KOREA" : zone == "saratov" || zone == "ukraine" ? "SARATOV" : "AFGHAN";
  load("MENUSCRN/PAYLOAD.LBM", payload_, -1);
  for (const Sprite& s : kSprites) {
    load(std::string("MENUSCRN/") + s.left, sprites_[int(s.store)][int(s.pylon)][0], 0);
    load(std::string("MENUSCRN/") + s.right, sprites_[int(s.store)][int(s.pylon)][1], 0);
  }
  for (int side = 0; side < 2; ++side) {
    gshgGround_[side].upload(rgbaPicture(kGshgGround, kGshgGroundW, kGshgGroundH, side == 1));
    gshgWindow_[side].upload(rgbaPicture(kGshgWindow, kGshgWindowW, kGshgWindowH, side == 1));
  }
  interceptorGround_.upload(interceptorPicture(7, 3, 4));
  interceptorPylon_.upload(interceptorPicture(5, 2, 6));
  // The close box as the original has it: 16 x 16 raw pixels in its interface colours.
  gamedata::PaletteRange gui;
  gui.first = 0xf2;
  for (const glm::vec4& c : {kGui2, kGui3, kGui4, kGui5, kGui6, kWhite})
    for (int k = 0; k < 3; ++k) gui.rgb.push_back((unsigned char)std::lround(c[k] * 255.0f));
  const std::pair<const char*, render::Texture*> boxes[] = {
      {"UIDATA/CLOSEOFF.BM", &closeOff_}, {"UIDATA/CLOSEON.BM", &closeOn_}, {"UIDATA/RESIZE.BM", &resize_}};
  for (const auto& [name, tex] : boxes) {
    gamedata::Picture pic;
    std::string error;
    const std::string path = file(name);
    if (!path.empty() && gamedata::loadRaw(path, 16, gui, pic, error)) tex->upload(pic);
  }
  // The zone's map (its features, heights and colours).
  map_ = {};
  facet_.clear();
  std::string error;
  if (!originalDir.empty() && !gamedata::loadZoneMap(originalDir, zoneDir + ".WZ", map_, error))
    std::fprintf(stderr, "%s\n", error.c_str());
  if (map_.loaded()) {
    // The facets' shades: lit from the north-west, flat ground the darkest; water.
    const int n = map_.size;
    facet_.assign(size_t(n) * size_t(n) * 2, 0);
    const glm::vec3 sun = glm::normalize(glm::vec3(-1.0f, 1.0f, 1.0f));  // x east, y north, z up
    const float step = map_.spacing / gamedata::kFeet;
    for (int i = 0; i + 1 < n; ++i)
      for (int j = 0; j + 1 < n; ++j) {
        const bool even = (i + j) % 2 == 0;
        const glm::ivec2 tris[2][3] = {
            {{i, j}, {i + 1, j}, even ? glm::ivec2(i + 1, j + 1) : glm::ivec2(i, j + 1)},
            {even ? glm::ivec2(i, j) : glm::ivec2(i + 1, j), {i + 1, j + 1}, {i, j + 1}}};
        for (int t = 0; t < 2; ++t) {
          auto p = [&](int k) { return glm::vec3(float(tris[t][k].x) * step, float(tris[t][k].y) * step, map_.height(tris[t][k].x, tris[t][k].y)); };
          const glm::vec3 nrm = glm::normalize(glm::cross(p(1) - p(0), p(2) - p(0)));
          const glm::vec3 up = nrm.z < 0.0f ? -nrm : nrm;
          const float lit = glm::dot(up, sun) - sun.z;
          const uint8_t colour = map_.colour[size_t(t)][size_t(i) * size_t(n) + size_t(j)];
          // Water: its colours, at sea level (the same colours are rock in the mountains).
          const bool water = colour >= 127 && colour <= 134 && std::max({p(0).z, p(1).z, p(2).z}) <= 0.0f;
          facet_[(size_t(i) * size_t(n) + size_t(j)) * 2 + size_t(t)] =
              water ? 255 : uint8_t(std::clamp(int(std::lround(lit * 40.0f)), 0, 6));
        }
      }
  }
  ready_ = true;
  return true;
}

void Planner::start(const gamedata::Mission& mission, glm::vec2 origin, const world::Terrain& terrain,
                    const sim::Targets& targets, const sim::Loadout& loadout,
                    const std::vector<std::string>& targetNames) {
  mission_ = &mission;
  terrain_ = &terrain;
  targets_ = &targets;
  origin_ = origin;
  loadout_ = loadout;
  targetNames_ = targetNames;
  missionRoute();
  saved_.clear();
  windows_.clear();
  page_ = 0;
  selected_ = dragging_ = held_ = closing_ = -1;
  panning_ = false;
  pylon_ = sim::Pylon::Inner;
  door_ = -1;
  note_.clear();
  status_.clear();
  closeMenu();
  texMpp_ = 0.0f;
  turning_ = boxing_ = false;
  mode_ = MapMode::Disable;
  debrief_ = askLog_ = false;
  fitPending_ = true;  // once the screen's size is known (draw)
}

void Planner::debrief(const std::string& title, const std::vector<std::string>& paragraphs) {
  debrief_ = true;
  askLog_ = false;
  debriefTitle_ = title;
  debriefText_ = paragraphs;
  debriefTop_ = 0, scrollGrab_ = -1.0f;
  windows_.clear();
  selected_ = dragging_ = held_ = closing_ = -1;
  mode_ = MapMode::Disable;
  closeMenu();
  status_.clear();
  open(Screen::Debrief);
}

void Planner::missionRoute() {
  route_.clear();
  if (const gamedata::MissionDrone* d = mission_ ? mission_->player() : nullptr)
    for (size_t i = 0; i < d->route.size(); ++i) {
      const gamedata::MissionWaypoint& w = d->route[i];
      route_.push_back({{w.pos.x - origin_.x, origin_.y - w.pos.y}, w, w.type == 1 || w.type == 3,
                        i < targetNames_.size() ? targetNames_[i] : std::string()});
    }
}

void Planner::applyRoute(gamedata::Mission& mission) const {
  gamedata::MissionDrone* d = mission.player();
  if (!d || route_.size() < 2) return;
  std::vector<gamedata::MissionWaypoint> r;
  for (const PlanPoint& p : route_) {
    gamedata::MissionWaypoint w = p.source;
    w.pos = {p.pos.x + origin_.x, origin_.y - p.pos.y};
    r.push_back(w);
  }
  d->route = r;
  gamedata::followPlayerRoute(mission);
}

// ---------------------------------------------------------------- windows

void Planner::open(Screen s) {
  for (size_t i = 0; i < windows_.size(); ++i)
    if (windows_[i].kind == s) {
      const Window w = windows_[i];
      windows_.erase(windows_.begin() + long(i));
      windows_.push_back(w);
      return;
    }
  // Where the original puts them (measured on its screens).
  Window w{s, {5, 5}, {551, 410}};
  switch (s) {
    case Screen::Briefing:
    case Screen::Payload: break;
    case Screen::Way: w.lo = {34, 0}, w.hi = {237, 107}; break;
    case Screen::WayInfo: w.lo = {115, 149}, w.hi = {352, 318}; break;
    case Screen::Key: w.lo = {0, 0}, w.hi = {101, screen_.y}; break;
    case Screen::Met: w.lo = {10, 10}, w.hi = {221, 101}; break;
    case Screen::Task: w.lo = {10, 10}, w.hi = {311, 101}; break;
    case Screen::Point: w.lo = {10, 10}, w.hi = {191, 86}; break;
    case Screen::View3D: w.lo = {69, 176}, w.hi = {311, 378}; break;
    case Screen::Debrief: w.lo = {73, 287}, w.hi = {573, 480}; break;
  }
  if (s == Screen::View3D) {
    // At the waypoint chosen (or the departure), the camera there.
    target3D_ = selected_ >= 0 && selected_ < int(route_.size()) ? route_[size_t(selected_)].pos
                : route_.empty() ? glm::vec2(0.0f) : route_[0].pos;
    cam3D_ = target3D_ + glm::vec2(0.0f, 600.0f);
    heading3D_ = 0.0f;
  }
  if (s == Screen::Briefing) page_ = 0;
  if (s == Screen::WayInfo && selected_ < 0 && !route_.empty()) selected_ = 0;
  windows_.push_back(w);
}

glm::vec2 Planner::minSize(Screen s) {
  switch (s) {
    case Screen::Briefing: return {200.0f, 120.0f};
    case Screen::View3D: return {150.0f, 100.0f};
    case Screen::Way: return {150.0f, 50.0f};
    case Screen::WayInfo: return {237.0f, 169.0f};
    case Screen::Key: return {101.0f, 200.0f};
    case Screen::Met: return {211.0f, 91.0f};
    case Screen::Task: return {240.0f, 70.0f};
    case Screen::Point: return {181.0f, 76.0f};
    case Screen::Debrief: return {200.0f, 80.0f};
    case Screen::Payload: break;
  }
  return {546.0f, 405.0f};
}

void Planner::close(Screen s) {
  if (s == Screen::Way && mode_ == MapMode::Way) mode_ = MapMode::Disable;
  windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [&](const Window& w) { return w.kind == s; }), windows_.end());
  if (s == Screen::Payload) closeMenu();
}

bool Planner::isOpen(Screen s) const {
  return std::any_of(windows_.begin(), windows_.end(), [&](const Window& w) { return w.kind == s; });
}

void Planner::toggle(Screen s) {
  if (isOpen(s) && windows_.back().kind == s) close(s);
  else open(s);
}

Planner::Window* Planner::windowAt(glm::vec2 p) {
  for (size_t i = windows_.size(); i-- > 0;)
    if (inside(p, windows_[i].lo, windows_[i].hi)) return &windows_[i];
  return nullptr;
}

// ---------------------------------------------------------------- the tool box

std::vector<Planner::Button> Planner::buttons() const {
  std::vector<Button> b;
  const glm::vec2 dx{toolX() - kToolBoxX, 0.0f};
  if (debrief_) {
    // After the mission only these two (as the original's).
    b.push_back({"Debrief", glm::vec2(557, 0) + dx, glm::vec2(640, 19) + dx, kDebrief, isOpen(Screen::Debrief)});
    b.push_back({"Exit", screen_ - glm::vec2(56, 16), screen_, kExit});
    return b;
  }
  b.push_back({"Briefing", glm::vec2(557, 0) + dx, glm::vec2(640, 19) + dx, kBriefing});
  b.push_back({"Takeoff", glm::vec2(557, 20) + dx, glm::vec2(640, 39) + dx, kTakeOff});
  static const char* labels[7][2] = {{"Zoom", "Rotate"}, {"Way", "Disable"}, {"Back", "Fit"}, {"Key", "Met"},
                                     {"3D", "Pay"},      {"Point", "Task"},  {"Save", "Restore"}};
  for (int r = 0; r < 7; ++r)
    for (int c = 0; c < 2; ++c) {
      const int id = kZoom + r * 2 + c;
      b.push_back({labels[r][c], glm::vec2(kToolX[c], kToolY[r]) + dx, glm::vec2(kToolX[c] + kToolW[c], kToolY[r] + kToolH) + dx, id,
                   (id == kZoom && mode_ == MapMode::Zoom) || (id == kRotate && mode_ == MapMode::Rotate) ||
                       (id == kWay && mode_ == MapMode::Way) || (id == kDisable && mode_ == MapMode::Disable)});
    }
  b.push_back({"Exit", screen_ - glm::vec2(56, 16), screen_, kExit});
  return b;
}

Result Planner::press(int id) {
  status_.clear();
  switch (id) {
    case kBriefing: toggle(Screen::Briefing); break;
    case kDebrief: toggle(Screen::Debrief); break;
    case kTakeOff: return Result::TakeOff;
    case kExit: return Result::Quit;
    case kZoom:
      // Closer, and the mouse draws rectangles to zoom into.
      zoom(1.0f / 1.5f, toMap(centre_));
      mode_ = MapMode::Zoom;
      break;
    case kBack: back(); break;
    case kFit: fit(); break;
    case kWay:
      // The waypoints' window, and clicks on the map move the one chosen in it.
      open(Screen::Way);
      mode_ = MapMode::Way;
      break;
    case kKey: toggle(Screen::Key); break;
    case kMet: toggle(Screen::Met); break;
    case kPay: toggle(Screen::Payload); break;
    case kTask: toggle(Screen::Task); break;
    case kDisable:
      // The map does nothing with the mouse; the waypoints' window away.
      close(Screen::Way);
      close(Screen::WayInfo);
      mode_ = MapMode::Disable;
      break;
    case kSave:
      saved_ = route_;
      status_ = "Route saved";
      break;
    case kRestore:
      if (!saved_.empty()) {
        route_ = saved_;
        status_ = "Saved route restored";
      } else if (mission_) {
        // Nothing saved: the mission's own route.
        missionRoute();
        status_ = "Mission route restored";
      }
      selected_ = -1;
      break;
    case kPoint: toggle(Screen::Point); break;
    case k3D: toggle(Screen::View3D); break;
    case kRotate: mode_ = MapMode::Rotate; break;
  }
  return Result::None;
}

void Planner::bevel(glm::vec2 lo, glm::vec2 hi, bool pressed, glm::vec4 face) {
  // As the original's boxes (CLOSEOFF.BM raised, CLOSEON.BM pressed): the top and left edges
  // light with a white corner, the bottom and right ones dark; the other way round pressed.
  const glm::vec2 last = hi - glm::vec2(1.0f);
  canvas_.fill(lo, hi, face);
  const glm::vec4 top = pressed ? kGui3 : kGui6, bottom = pressed ? kGui6 : kGui3;
  canvas_.fill(lo, {hi.x, lo.y + 1}, top);
  canvas_.fill(lo, {lo.x + 1, hi.y}, top);
  canvas_.fill({lo.x, last.y}, hi, bottom);
  canvas_.fill({last.x, lo.y}, hi, bottom);
  canvas_.fill(lo, lo + glm::vec2(1.0f), pressed ? kGui2 : kWhite);
  canvas_.fill({last.x, lo.y}, {hi.x, lo.y + 1}, kGui4);
  canvas_.fill({lo.x, last.y}, {lo.x + 1, hi.y}, kGui4);
  canvas_.fill(last, hi, pressed ? kWhite : kGui2);
}

void Planner::label(const std::string& s, glm::vec2 lo, glm::vec2 hi) {
  // Capitals 8 pixels high, in the middle.
  canvas_.text(s, {std::floor((lo.x + hi.x) * 0.5f), std::floor((lo.y + hi.y - 8.0f) * 0.5f)}, kWhite, 0);
}

void Planner::drawToolBox() {
  const glm::vec2 dx{toolX() - kToolBoxX, 0.0f};
  if (!debrief_) bevel(kPanelLo + dx, kPanelHi + dx, false, kGui4);
  if (!debrief_) canvas_.text("Map Tool Box", {std::floor((kPanelLo.x + kPanelHi.x) * 0.5f + dx.x), kPanelLo.y + 4.0f}, kWhite, 0);
  for (const Button& b : buttons()) {
    const bool down = b.down || (held_ == b.id && inside(mouse_, b.lo, b.hi));
    bevel(b.lo, b.hi, down, kGui5);
    const glm::vec2 d = down ? glm::vec2(1.0f) : glm::vec2(0.0f);
    label(b.label, b.lo + d, b.hi + d);
  }
  if (!status_.empty()) {
    const glm::vec2 lo{7.0f, 458.0f}, hi{lo.x + canvas_.textWidth(status_) + 16.0f, 477.0f};
    canvas_.fill(lo, hi, kGui5);
    canvas_.text(status_, {lo.x + 8.0f, lo.y + 6.0f}, kWhite);
  }
}

// ---------------------------------------------------------------- events

namespace {
// The log question's box in the middle of the screen, its three buttons.
struct LogBox {
  glm::vec2 lo, hi, button[3][2];
};
LogBox logBox(glm::vec2 screen) {
  LogBox b;
  b.lo = glm::floor(screen * 0.5f - glm::vec2(150.0f, 45.0f)), b.hi = b.lo + glm::vec2(300.0f, 90.0f);
  for (int i = 0; i < 3; ++i) {
    b.button[i][0] = b.lo + glm::vec2(20.0f + 92.0f * float(i), 52.0f);
    b.button[i][1] = b.button[i][0] + glm::vec2(76.0f, 22.0f);
  }
  return b;
}
const char* const kLogAnswers[3] = {"Yes", "Retry", "No"};
}  // namespace

Result Planner::handleEvent(const SDL_Event& e) {
  if (askLog_) {
    // Only the question's answers now.
    const Result answers[3] = {Result::LogYes, Result::LogRetry, Result::LogNo};
    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONUP) {
      const glm::vec2 window = e.type == SDL_MOUSEMOTION ? glm::vec2(e.motion.x, e.motion.y) : glm::vec2(e.button.x, e.button.y);
      mouse_ = canvas_.toScreen(window * mouseScale_);
      if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const LogBox b = logBox(screen_);
        for (int i = 0; i < 3; ++i)
          if (inside(mouse_, b.button[i][0], b.button[i][1])) return askLog_ = false, answers[i];
      }
    }
    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      const SDL_Scancode k = e.key.keysym.scancode;
      if (k == SDL_SCANCODE_Y || k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) return askLog_ = false, Result::LogYes;
      if (k == SDL_SCANCODE_R) return askLog_ = false, Result::LogRetry;
      if (k == SDL_SCANCODE_N || k == SDL_SCANCODE_ESCAPE) return askLog_ = false, Result::LogNo;
    }
    return Result::None;
  }
  Window* front = windows_.empty() ? nullptr : &windows_.back();
  if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
    const glm::vec2 window = e.type == SDL_MOUSEMOTION ? glm::vec2(e.motion.x, e.motion.y) : glm::vec2(e.button.x, e.button.y);
    const glm::vec2 was = mouse_;
    mouse_ = canvas_.toScreen(window * mouseScale_);
    if (e.type == SDL_MOUSEMOTION) {
      if (moving_ >= 0 && moving_ < int(windows_.size())) {
        // Kept on the screen.
        Window& w = windows_[size_t(moving_)];
        const glm::vec2 size = w.hi - w.lo;
        w.lo = glm::clamp(mouse_ - grab_, glm::vec2(0.0f), screen_ - size);
        w.hi = w.lo + size;
        return Result::None;
      }
      if (sizing_ >= 0 && sizing_ < int(windows_.size())) {
        Window& w = windows_[size_t(sizing_)];
        w.hi = glm::clamp(mouse_ + grab_, w.lo + minSize(w.kind), screen_);
        return Result::None;
      }
      if (scrollGrab_ >= 0.0f) {
        // The thumb dragged along the bar: the text goes with it.
        const float travel = std::max(1.0f, scrollHi_.y - scrollLo_.y - (thumbHi_ - thumbLo_));
        const int room = std::max(0, debriefTotal_ - debriefFit_);
        debriefTop_ = std::clamp(int(std::lround((mouse_.y - scrollGrab_ - scrollLo_.y) / travel * float(room))), 0, room);
        return Result::None;
      }
      if (dragging_ >= 0) route_[size_t(dragging_)].pos = toWorld(mouse_);
      else if (turning_) rot_ = grabRot_ + std::atan2(mouse_.y - screen_.y * 0.5f, mouse_.x - screen_.x * 0.5f) - grabAngle_;
      else if (panning_) centre_ -= turn(mouse_ - was, -rot_) * mpp_;
      return Result::None;
    }
    if (e.type == SDL_MOUSEBUTTONUP) {
      // A click on the map (not a drag) with the 3D window open: the place to show.
      if (pressed3D_ && isOpen(Screen::View3D) && glm::distance(mouse_, pressAt_) < 3.0f) target3D_ = toWorld(mouse_);
      if (boxing_) {
        // Zoom: drawn to the right, the rectangle's area fills the screen (in); to the left,
        // the screen shrinks into the rectangle (out).
        const glm::vec2 half = zoomBoxHalf();
        if (half.x >= 3.0f) {
          const float f = half.x * 2.0f / screen_.x;
          centre_ = toWorld(pressAt_);
          mpp_ = std::clamp(mouse_.x >= pressAt_.x ? mpp_ * f : mpp_ / f, 4.0f, 600.0f);
        }
      }
      dragging_ = moving_ = sizing_ = -1;
      panning_ = turning_ = boxing_ = pressed3D_ = false;
      scrollGrab_ = -1.0f;
      // A button acts, and a window closes, when let go over it (as the original's do).
      Result r = Result::None;
      if (held_ >= 0)
        for (const Button& b : buttons())
          if (b.id == held_ && inside(mouse_, b.lo, b.hi)) r = press(b.id);
      if (held_ <= -2)
        for (const Window& w : windows_)
          if (w.kind == Screen::Way && inside(mouse_, w.lo + kWayButtonLo[-2 - held_], w.lo + kWayButtonHi[-2 - held_])) {
            wayButton(-2 - held_);
            break;
          }
      if (closing_ >= 0 && closing_ < int(windows_.size())) {
        const Window& w = windows_[size_t(closing_)];
        if (inside(mouse_, w.lo, w.lo + glm::vec2(16.0f))) close(w.kind);
      }
      held_ = closing_ = -1;
      return r;
    }
    if (e.button.button != SDL_BUTTON_LEFT) {
      // The middle button drags the map along (whatever the tool box's mode).
      if (e.button.button == SDL_BUTTON_MIDDLE && !windowAt(mouse_) && mouse_.x < toolX()) panning_ = true;
      // Right button: a digit of the waypoint's information down; on the map, moves it.
      if (e.button.button != SDL_BUTTON_RIGHT) return Result::None;
      if (Window* w = windowAt(mouse_)) {
        if (w->kind == Screen::WayInfo) wayInfoClick(*w, mouse_, true);
      } else if (mouse_.x < toolX() && !inside(mouse_, kPanelLo + glm::vec2(toolX() - kToolBoxX, 0.0f), screen_)) {
        // As in the original: the map moves straight towards the side clicked (left, right,
        // up or down, whichever the click is furthest towards), by half the screen.
        const glm::vec2 d = mouse_ - screen_ * 0.5f;
        const glm::vec2 step = std::fabs(d.x) / screen_.x >= std::fabs(d.y) / screen_.y
                                   ? glm::vec2(d.x > 0.0f ? screen_.x * 0.5f : -screen_.x * 0.5f, 0.0f)
                                   : glm::vec2(0.0f, d.y > 0.0f ? screen_.y * 0.5f : -screen_.y * 0.5f);
        centre_ += turn(step, -rot_) * mpp_;
      }
      return Result::None;
    }
    if (menuOpen()) {
      // A click in the pylon menu hangs the store there (the door gun in the door); anywhere
      // else closes it.
      const glm::vec2 lo = menuLo();
      for (size_t i = 0; i < menuItems(); ++i)
        if (inside(mouse_, lo + glm::vec2(0.0f, 1.0f + kMenuItemH * float(i)), lo + glm::vec2(kMenuW, 1.0f + kMenuItemH * float(i + 1)))) {
          if (menuDoors_) {
            for (int d = 0; d < 2; ++d)
              if (i == 2 || int(i) == d) loadout_.doorGun[d] = true;
            door_ = i == 1 ? 1 : 0;
            note_.clear();
          } else {
            pylon_ = menuPylons_[i];
            door_ = -1;
            choose(menuStore_);
          }
        }
      closeMenu();
      return Result::None;
    }
    for (const Button& b : buttons())
      if (inside(mouse_, b.lo, b.hi)) {
        held_ = b.id;
        return Result::None;
      }
    if (inside(mouse_, kPanelLo + glm::vec2(toolX() - kToolBoxX, 0.0f), kPanelHi + glm::vec2(toolX() - kToolBoxX, 0.0f))) return Result::None;
    if (Window* w = windowAt(mouse_)) {
      const int index = int(w - windows_.data());
      if (inside(mouse_, w->lo, w->lo + glm::vec2(16.0f))) {
        closing_ = index;
        return Result::None;
      }
      // To the front, then what it does with the click. The grip in the bottom right corner
      // resizes it, the title bar (the top row) moves it.
      const Window copy = *w;
      if (index + 1 != int(windows_.size())) open(copy.kind);
      const int front = int(windows_.size()) - 1;
      if (resizable(copy.kind) && inside(mouse_, copy.hi - glm::vec2(17.0f), copy.hi)) {
        sizing_ = front;
        grab_ = copy.hi - mouse_;
        return Result::None;
      }
      if (copy.kind != Screen::Payload && mouse_.y < copy.lo.y + 18.0f) {
        moving_ = front;
        grab_ = mouse_ - copy.lo;
      }
      switch (copy.kind) {
        case Screen::Payload: payloadClick(mouse_ - copy.lo - glm::vec2(2.0f)); break;
        case Screen::Way:
          wayClick(copy, mouse_);
          if (held_ <= -2) moving_ = -1;  // a button in the top row, not the title
          break;
        case Screen::WayInfo: wayInfoClick(copy, mouse_, false); break;
        case Screen::Key: keyClick(copy, mouse_); break;
        case Screen::Task: taskClick(copy, mouse_); break;
        case Screen::View3D: click3D(copy, mouse_); break;
        case Screen::Debrief: debriefClick(mouse_); break;
        default: break;
      }
      return Result::None;
    }
    // The map, as the tool box's mode has it.
    pressAt_ = mouse_;
    pressed3D_ = true;  // a click without moving: the place for the 3D view
    switch (mode_) {
      case MapMode::Zoom: boxing_ = true; break;
      case MapMode::Rotate:
        // The map turns as far round the middle as the mouse goes round it.
        turning_ = true;
        grabAngle_ = std::atan2(mouse_.y - screen_.y * 0.5f, mouse_.x - screen_.x * 0.5f);
        grabRot_ = rot_;
        break;
      case MapMode::Way:
        // The waypoint chosen in the window goes where the map is clicked, and follows the
        // mouse while it is held (not the departure and the landing).
        pressed3D_ = false;
        if (selected_ >= 0 && selected_ < int(route_.size()) && !route_[size_t(selected_)].fixed) {
          route_[size_t(selected_)].pos = toWorld(mouse_);
          dragging_ = selected_;
        }
        break;
      case MapMode::Disable: break;
    }
    return Result::None;
  }
  if (e.type == SDL_MOUSEWHEEL) {
    Window* w = windowAt(mouse_);
    if (w && w->kind == Screen::Briefing) page_ = std::clamp(page_ - e.wheel.y, 0, pages_ - 1);
    else if (w && w->kind == Screen::Debrief) scrollDebrief(-3 * e.wheel.y);
    else if (!w && mouse_.x < toolX()) zoom(e.wheel.y > 0 ? 1.0f / 1.25f : 1.25f, mouse_);
    return Result::None;
  }
  if (e.type != SDL_KEYDOWN) return Result::None;

  const SDL_Scancode sc = e.key.keysym.scancode;
  const bool shift = e.key.keysym.mod & KMOD_SHIFT;
  if (debrief_) {
    // Enter and Esc out (to the menus), D the debriefing's window; the map's keys as before.
    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_ESCAPE) return e.key.repeat ? Result::None : Result::Quit;
    if (sc == SDL_SCANCODE_D) return press(kDebrief);
    // The arrows and the page keys scroll its text.
    if (isOpen(Screen::Debrief)) {
      if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN) return scrollDebrief(sc == SDL_SCANCODE_UP ? -1 : 1), Result::None;
      if (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN)
        return scrollDebrief((sc == SDL_SCANCODE_PAGEUP ? -1 : 1) * std::max(1, debriefFit_ - 1)), Result::None;
      if (sc == SDL_SCANCODE_HOME || sc == SDL_SCANCODE_END) return scrollDebrief(sc == SDL_SCANCODE_HOME ? -debriefTotal_ : debriefTotal_), Result::None;
    }
    if (sc == SDL_SCANCODE_B || sc == SDL_SCANCODE_P || sc == SDL_SCANCODE_W || sc == SDL_SCANCODE_T || sc == SDL_SCANCODE_S ||
        sc == SDL_SCANCODE_R || sc == SDL_SCANCODE_3 || sc == SDL_SCANCODE_O)
      return Result::None;
  }
  if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) return e.key.repeat ? Result::None : Result::TakeOff;
  if (sc == SDL_SCANCODE_ESCAPE) {
    // The pylon menu, the front window, the waypoint chosen, then out.
    if (menuOpen()) closeMenu();
    else if (front) close(front->kind);
    else if (selected_ >= 0) selected_ = -1;
    else return e.key.repeat ? Result::None : Result::Quit;
    return Result::None;
  }
  // The tool box's buttons by their initials.
  switch (sc) {
    case SDL_SCANCODE_B: return press(kBriefing);
    case SDL_SCANCODE_P: return press(kPay);
    case SDL_SCANCODE_W:
      if (shift) {
        open(Screen::WayInfo);
        return Result::None;
      }
      return press(kWay);
    case SDL_SCANCODE_K: return press(kKey);
    case SDL_SCANCODE_M: return press(kMet);
    case SDL_SCANCODE_T: return press(kTask);
    case SDL_SCANCODE_O: return press(kPoint);
    case SDL_SCANCODE_3: if (!front || front->kind != Screen::Payload) return press(k3D); break;
    case SDL_SCANCODE_D: return press(kDisable);
    case SDL_SCANCODE_S: return press(kSave);
    case SDL_SCANCODE_R: return press(kRestore);
    case SDL_SCANCODE_F:
    case SDL_SCANCODE_HOME: return press(kFit);
    case SDL_SCANCODE_KP_PLUS:
    case SDL_SCANCODE_EQUALS: return press(kZoom);
    case SDL_SCANCODE_KP_MINUS:
    case SDL_SCANCODE_MINUS: return press(kBack);
    case SDL_SCANCODE_G: showGrid_ = !showGrid_; return Result::None;
    case SDL_SCANCODE_COMMA:
    case SDL_SCANCODE_PERIOD:
      // The map turned by 15 degrees.
      rot_ += (sc == SDL_SCANCODE_PERIOD ? 1.0f : -1.0f) * 0.2617994f;
      return Result::None;
    default: break;
  }

  // The rest goes to the front window: the briefing's pages, the payload's pylons and stores;
  // without one to the map.
  if (front && front->kind == Screen::Briefing) {
    if (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_LEFT) page_ = std::max(0, page_ - 1);
    if (sc == SDL_SCANCODE_PAGEDOWN || sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_SPACE)
      page_ = std::min(pages_ - 1, page_ + 1);
    return Result::None;
  }
  if (front && front->kind == Screen::Payload) {
    // The pylon pairs, then the left and the right door.
    int p = door_ >= 0 ? sim::kPylons + door_ : int(pylon_);
    if (sc == SDL_SCANCODE_UP) p = std::max(0, p - 1);
    if (sc == SDL_SCANCODE_DOWN) p = std::min(sim::kPylons + 1, p + 1);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_5) p = sc - SDL_SCANCODE_1;
    if (p != (door_ >= 0 ? sim::kPylons + door_ : int(pylon_))) {
      if (p < sim::kPylons) pylon_ = sim::Pylon(p), door_ = -1;
      else door_ = p - sim::kPylons;
      note_.clear();
    }
    if (door_ >= 0) {
      // The door gun in or out.
      if (sc == SDL_SCANCODE_N || sc == SDL_SCANCODE_DELETE) loadout_.doorGun[door_] = false;
      if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT) loadout_.doorGun[door_] = !loadout_.doorGun[door_];
      return Result::None;
    }
    if (sc == SDL_SCANCODE_N || sc == SDL_SCANCODE_DELETE) choose(sim::Store::None);
    if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT) {
      // The next store that fits this pylon and flies.
      const int step = sc == SDL_SCANCODE_RIGHT ? 1 : sim::kStores - 1;
      int s = int(loadout_.at(pylon_));
      for (int i = 0; i < sim::kStores; ++i) {
        s = (s + step) % sim::kStores;
        if (sim::storeFits(sim::Store(s), pylon_) && sim::storeInfo(sim::Store(s)).available) break;
      }
      choose(sim::Store(s));
    }
    return Result::None;
  }
  if (front && front->kind == Screen::View3D && (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN)) {
    // Sat, Point, Fly in turn.
    mode3D_ = Mode3D((int(mode3D_) + (sc == SDL_SCANCODE_PAGEDOWN ? 1 : 2)) % 3);
    if (mode3D_ == Mode3D::Fly) cam3D_ = target3D_ + glm::vec2(0.0f, 600.0f), heading3D_ = 0.0f;
    return Result::None;
  }
  if (front && front->kind == Screen::WayInfo && selected_ >= 0 && (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN)) {
    // The waypoint's speed by 10 knots, with Shift its delay by 5 seconds.
    PlanPoint& q = route_[size_t(selected_)];
    const float sign = sc == SDL_SCANCODE_PAGEUP ? 1.0f : -1.0f;
    if (shift) q.source.wait = std::clamp(q.source.wait + 5.0f * sign, 0.0f, 9999.0f);
    else q.source.speed = std::clamp(q.source.speed + 10.0f * kKnot * sign, 0.0f, 9999.0f * kKnot);
    return Result::None;
  }
  // Editing the route as with the mouse: only with the waypoints' window open (Way).
  const bool editing = mode_ == MapMode::Way;
  if (editing && (sc == SDL_SCANCODE_INSERT || sc == SDL_SCANCODE_I)) wayButton(0);
  if (editing && (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE)) wayButton(1);
  if (sc == SDL_SCANCODE_TAB && !route_.empty()) {
    const int n = int(route_.size());
    selected_ = selected_ < 0 ? (shift ? n - 1 : 1 % n) : (selected_ + (shift ? n - 1 : 1)) % n;
    if (isOpen(Screen::View3D)) target3D_ = route_[size_t(selected_)].pos;  // the 3D view goes there
  }
  // Arrows move the selected waypoint, or the map without one; Shift: five times as far.
  glm::vec2 d{0.0f};
  if (sc == SDL_SCANCODE_LEFT) d.x = -1;
  if (sc == SDL_SCANCODE_RIGHT) d.x = 1;
  if (sc == SDL_SCANCODE_UP) d.y = -1;
  if (sc == SDL_SCANCODE_DOWN) d.y = 1;
  if (d != glm::vec2(0.0f)) {
    d = turn(d, -rot_) * (shift ? 20.0f : 4.0f) * mpp_;
    if (editing && selected_ >= 0) {
      if (!route_[size_t(selected_)].fixed) route_[size_t(selected_)].pos += d;
    } else {
      centre_ += d * 2.0f;
    }
  }
  return Result::None;
}

// ---------------------------------------------------------------- drawing

glm::vec2 Planner::windowPoint(glm::vec2 screen) const {
  return canvas_.toWindow(screen) / mouseScale_;
}

void Planner::draw(int w, int h, float dt) {
  if (isOpen(Screen::View3D)) step3D(dt);
  canvas_.beginFixed(w, h, uiScale_);
  screen_ = glm::floor(canvas_.size());
  if (fitPending_) fit(), fitPending_ = false;
  // The windows kept on a screen that got smaller.
  for (Window& win : windows_) {
    const glm::vec2 size = glm::min(win.hi - win.lo, screen_);
    win.lo = glm::clamp(win.lo, glm::vec2(0.0f), screen_ - size);
    win.hi = win.lo + size;
  }
  drawMap(dt);
  for (const Window& win : windows_) drawWindow(win);
  drawToolBox();
  if (askLog_) {
    const LogBox b = logBox(screen_);
    bevel(b.lo, b.hi, false, kGui5);
    canvas_.text("Do you wish to log this mission?", {std::floor((b.lo.x + b.hi.x) * 0.5f), b.lo.y + 18.0f}, kWhite, 0);
    for (int i = 0; i < 3; ++i) {
      const bool over = inside(mouse_, b.button[i][0], b.button[i][1]);
      bevel(b.button[i][0], b.button[i][1], false, over ? kGui4 : kGui3);
      canvas_.text(kLogAnswers[i], {std::floor((b.button[i][0].x + b.button[i][1].x) * 0.5f), b.button[i][0].y + 4.0f}, kWhite, 0);
    }
  }
  canvas_.end();
}

void Planner::drawWindow(const Window& w) {
  switch (w.kind) {
    case Screen::Briefing: drawBriefing(w); break;
    case Screen::Payload: drawPayload(w); break;
    case Screen::Way: drawWay(w); break;
    case Screen::WayInfo: drawWayInfo(w); break;
    case Screen::Key: drawKey(w); break;
    case Screen::Met: drawMet(w); break;
    case Screen::Task: drawTask(w); break;
    case Screen::Point: drawPoint(w); break;
    case Screen::View3D: draw3D(w); break;
    case Screen::Debrief: drawDebrief(w); break;
  }
  // The grip for its size in the bottom right corner, the close box in the top left one.
  if (resizable(w.kind)) {
    // A raised triangle in the corner: light along its slope, dark below and right.
    const glm::vec2 c = w.hi - glm::vec2(2.0f);
    const float k = 11.0f;
    canvas_.triangle(c + glm::vec2(-k, 0.0f), c + glm::vec2(0.0f, -k), c, kGui6);
    canvas_.line(c + glm::vec2(-k, 0.5f), c + glm::vec2(0.5f, -k), kWhite);
    canvas_.fill(c + glm::vec2(-k, -1.0f), c, kGui2);
    canvas_.fill(c + glm::vec2(-1.0f, -k), c, kGui2);
  }
  const int index = int(&w - windows_.data());
  const bool down = closing_ == index && inside(mouse_, w.lo, w.lo + glm::vec2(16.0f));
  const render::Texture& box = down ? closeOn_ : closeOff_;
  if (box.loaded()) canvas_.image(box, w.lo);
  else bevel(w.lo, w.lo + glm::vec2(16.0f), down, kGui5);
}

void Planner::digits(glm::vec2 at, int count, long value) {
  // Boxes 12 pixels apart, a digit pressed into each.
  for (int i = count - 1; i >= 0; --i, value /= 10) {
    const glm::vec2 lo = at + glm::vec2(12.0f * float(i), 0.0f);
    bevel(lo, lo + glm::vec2(12.0f, 14.0f), true, kGui5);
    canvas_.text(std::string(1, char('0' + std::abs(value % 10))), lo + glm::vec2(6.0f, 3.0f), kWhite, 0);
  }
}

int Planner::digitAt(glm::vec2 at, int count, glm::vec2 p) const {
  if (!inside(p, at, at + glm::vec2(12.0f * float(count), 14.0f))) return -1;
  return int((p.x - at.x) / 12.0f);
}

// The waypoints' letters, a row of boxes (the chosen one pressed), and Info for its window.
namespace {
constexpr float kLetterX = 36.0f, kLetterY = 22.0f, kLetterStep = 20.0f;
constexpr int kLettersPerRow = 10;
glm::vec2 letterLo(glm::vec2 window, size_t i) {
  return window + glm::vec2(kLetterX - 34.0f + kLetterStep * float(i % kLettersPerRow), kLetterY + kLetterStep * float(i / kLettersPerRow));
}
// The waypoint window's digits: east, north (metres of the zone), height (feet); speed (knots),
// delay (seconds).
const glm::vec2 kEastAt{7.0f, 32.0f}, kNorthAt{89.0f, 32.0f}, kAltAt{171.0f, 32.0f}, kSpeedAt{67.0f, 80.0f},
                kDelayAt{70.0f, 104.0f};
}  // namespace

void Planner::drawWay(const Window& w) {
  // Ins, Del and Info along the top, the waypoints' letters below, ten to a row.
  bevel(w.lo, w.hi, false, kGui5);
  for (int k = 0; k < 3; ++k) {
    const glm::vec2 lo = w.lo + kWayButtonLo[k], hi = w.lo + kWayButtonHi[k];
    bevel(lo, hi, held_ == -2 - k && inside(mouse_, lo, hi), kGui5);
    label(kWayButton[k], lo, hi);
  }
  for (size_t i = 0; i < route_.size(); ++i) {
    const glm::vec2 lo = letterLo(w.lo, i);
    const bool down = int(i) == selected_;
    bevel(lo, lo + glm::vec2(19.0f), down, kGui5);
    label(std::string(1, letter(i)), lo + glm::vec2(down ? 1.0f : 0.0f), lo + glm::vec2(down ? 20.0f : 19.0f));
  }
}

void Planner::wayClick(const Window& w, glm::vec2 p) {
  for (int k = 0; k < 3; ++k)
    if (inside(p, w.lo + kWayButtonLo[k], w.lo + kWayButtonHi[k])) {
      held_ = -2 - k;  // acts when let go over it
      return;
    }
  for (size_t i = 0; i < route_.size(); ++i)
    if (inside(p, letterLo(w.lo, i), letterLo(w.lo, i) + glm::vec2(19.0f))) selected_ = int(i);
}

void Planner::wayButton(int k) {
  // Ins: a new waypoint after the chosen one, half way to the next; Del: the chosen one away.
  if (k == 0 && selected_ >= 0) {
    const int to = selected_ + 1 < int(route_.size()) ? selected_ + 1 : selected_ - 1;
    if (to >= 0) insertAfter(std::min(selected_, to), (route_[size_t(selected_)].pos + route_[size_t(to)].pos) * 0.5f);
  }
  if (k == 1) erase(selected_);
  if (k == 2) open(Screen::WayInfo);
}

char Planner::letter(size_t i) const {
  // The targets X, Y, Z..., the other waypoints A, B, C... in their order.
  if (i >= route_.size()) return '?';
  const bool target = route_[i].source.type == 4;
  int n = 0;
  for (size_t k = 0; k < i; ++k)
    if ((route_[k].source.type == 4) == target) ++n;
  return target ? char('X' + n % 3) : char('A' + n % 26);
}

void Planner::drawWayInfo(const Window& w) {
  bevel(w.lo, w.hi, false, kGui5);
  if (selected_ < 0 || selected_ >= int(route_.size())) return;
  const PlanPoint& p = route_[size_t(selected_)];
  const glm::vec2 o = w.lo;
  canvas_.text(fmt("Waypoint %c", letter(size_t(selected_))) + (p.fixed ? " (Fixed)" : !p.what.empty() ? " (" + p.what + ")" : ""), o + glm::vec2(33.0f, 8.0f), kWhite);
  canvas_.text("Position", o + glm::vec2(5.0f, 20.0f), kWhite);
  const glm::vec2 zone{p.pos.x + origin_.x, origin_.y - p.pos.y};
  digits(o + kEastAt, 6, long(std::lround(zone.x)));
  digits(o + kNorthAt, 6, long(std::lround(zone.y)));
  digits(o + kAltAt, 5, long(std::lround(p.source.alt / gamedata::kFeet)));
  const int type = p.source.type;
  canvas_.text("Type", o + glm::vec2(5.0f, 60.0f), kWhite);
  bevel(o + glm::vec2(36.0f, 56.0f), o + glm::vec2(160.0f, 68.0f), true, kGui5);
  label(type >= 0 && type <= 4 ? kWaypointType[type] : type == 6 ? "Reconnaissance" : type == 17 ? "Mine laying" : "Troops",
        o + glm::vec2(36.0f, 56.0f), o + glm::vec2(160.0f, 68.0f));
  canvas_.text("Speed (Kt)", o + glm::vec2(5.0f, 83.0f), kWhite);
  digits(o + kSpeedAt, 4, long(std::lround(p.source.speed / kKnot)));
  canvas_.text("Total Delay", o + glm::vec2(5.0f, 107.0f), kWhite);
  digits(o + kDelayAt, 4, long(std::lround(p.source.wait)));
  // The time from the start, the leg to it.
  float time = 0.0f;
  for (int i = 1; i <= selected_; ++i) {
    const PlanPoint& q = route_[size_t(i)];
    time += glm::distance(route_[size_t(i - 1)].pos, q.pos) / std::max(q.source.speed, 10.0f) + q.source.wait;
  }
  const int t = int(time);
  canvas_.text(fmt("Time to this waypoint: %d:%02d:%02d", t / 3600, t / 60 % 60, t % 60), o + glm::vec2(5.0f, 132.0f), kWhite);
  const float leg = selected_ > 0 ? glm::distance(route_[size_t(selected_) - 1].pos, p.pos) : 0.0f;
  canvas_.text(fmt("Length this leg: %.0fft (%.0f miles)", leg / gamedata::kFeet, leg / 1609.34f), o + glm::vec2(5.0f, 156.0f),
               kWhite);
}

void Planner::wayInfoClick(const Window& w, glm::vec2 p, bool right) {
  // A digit up with the left button, down with the right (the departure and landing keep their
  // places, but not their speed and delay).
  if (selected_ < 0 || selected_ >= int(route_.size())) return;
  PlanPoint& q = route_[size_t(selected_)];
  const float sign = right ? -1.0f : 1.0f;
  auto step = [&](glm::vec2 at, int count, glm::vec2 pt) {
    const int d = digitAt(w.lo + at, count, pt);
    return d < 0 ? 0.0f : sign * std::pow(10.0f, float(count - 1 - d));
  };
  if (!q.fixed) {
    q.pos.x += step(kEastAt, 6, p);
    q.pos.y -= step(kNorthAt, 6, p);
    q.source.alt = std::max(0.0f, q.source.alt + step(kAltAt, 5, p) * gamedata::kFeet);
  }
  q.source.speed = std::clamp(q.source.speed + step(kSpeedAt, 4, p) * kKnot, 0.0f, 9999.0f * kKnot);
  q.source.wait = std::clamp(q.source.wait + step(kDelayAt, 4, p), 0.0f, 9999.0f);
}

// The map's layers, a column of boxes pressed in while on.
void Planner::drawKey(const Window& w) {
  bevel(w.lo, w.hi, false, kGui5);
  canvas_.fill(w.lo + glm::vec2(1.0f), {w.hi.x - 1.0f, w.lo.y + 18.0f}, kGui4);
  static const char* names[] = {"Grid Lines", "Waypoints", "Compass", "Allied AAA", "Allied SAM", "Enemy AAA", "Enemy SAM", "Sides"};
  const bool on[] = {showGrid_, showRoute_, showCompass_, alliedAaa_, alliedSam_, enemyAaa_, enemySam_, showForces_};
  for (int i = 0; i < 8; ++i) {
    const glm::vec2 lo = w.lo + glm::vec2(1.0f, 18.0f + 20.0f * float(i)), hi = lo + glm::vec2(99.0f, 19.0f);
    bevel(lo, hi, on[i], kGui5);
    label(names[i], lo, hi);
  }
}

void Planner::keyClick(const Window& w, glm::vec2 p) {
  bool* layers[] = {&showGrid_, &showRoute_, &showCompass_, &alliedAaa_, &alliedSam_, &enemyAaa_, &enemySam_, &showForces_};
  for (int i = 0; i < 8; ++i) {
    const glm::vec2 lo = w.lo + glm::vec2(1.0f, 18.0f + 20.0f * float(i));
    if (inside(p, lo, lo + glm::vec2(99.0f, 19.0f))) *layers[i] = !*layers[i];
  }
}

void Planner::drawMet(const Window& w) {
  bevel(w.lo, w.hi, false, kGui5);
  canvas_.text("Meteorological Information", w.lo + glm::vec2(20.0f, 5.0f), kWhite);
  if (!mission_) return;
  static const char* weather[] = {"Clear", "Overcast", "Light fog", "Thick fog", "Rain"};
  const std::string rows[3][2] = {{"Wind Strength", fmt("%.0f Kts", double(mission_->windSpeed))},
                                  {"Wind Direction", fmt("%.0f Degs", double(mission_->windDirection))},
                                  {"Weather condition", mission_->weather >= 0 && mission_->weather < 5 ? weather[mission_->weather] : "?"}};
  for (int i = 0; i < 3; ++i) {
    const glm::vec2 at = w.lo + glm::vec2(5.0f, 27.0f + 20.0f * float(i));
    canvas_.text(rows[i][0], at, kWhite);
    canvas_.text(":", at + glm::vec2(110.0f, 0.0f), kWhite);
    canvas_.text(rows[i][1], at + glm::vec2(125.0f, 0.0f), kWhite);
  }
}

std::vector<Planner::Flight> Planner::flights() const {
  // The helicopters of the player's task: his own (his call sign in the original: Immortal),
  // then the wingmen.
  std::vector<Flight> out;
  if (!mission_) return out;
  for (const gamedata::MissionTask& t : mission_->tasks) {
    if (!t.user) continue;
    for (size_t i = 0; i < t.drones.size(); ++i) {
      Flight f;
      f.name = std::string(i == 0 ? "Immortal" : "Wingman") + " : MI 24 Hind";
      if (i == 0)
        for (const PlanPoint& p : route_) f.route.push_back(p.pos);
      else
        for (const gamedata::MissionWaypoint& w : t.drones[i].route) f.route.push_back({w.pos.x - origin_.x, origin_.y - w.pos.y});
      out.push_back(f);
    }
  }
  return out;
}

void Planner::drawTask(const Window& w) {
  bevel(w.lo, w.hi, false, kGui5);
  canvas_.text("Task Box", w.lo + glm::vec2(20.0f, 11.0f), kWhite);
  const std::vector<Flight> f = flights();
  for (size_t i = 0; i < f.size(); ++i) {
    const glm::vec2 lo = w.lo + glm::vec2(20.0f, 56.0f + 17.0f * float(i));
    if (int(i) == flight_) canvas_.fill(lo, {w.hi.x - 2.0f, lo.y + 12.0f}, kGui4);
    canvas_.text(f[i].name, lo + glm::vec2(4.0f, 2.0f), kWhite);
  }
}

void Planner::taskClick(const Window& w, glm::vec2 p) {
  const size_t n = flights().size();
  for (size_t i = 0; i < n; ++i) {
    const glm::vec2 lo = w.lo + glm::vec2(20.0f, 56.0f + 17.0f * float(i));
    if (inside(p, lo, {w.hi.x - 2.0f, lo.y + 12.0f})) flight_ = int(i);
  }
}

void Planner::drawPoint(const Window& w) {
  // What is under the mouse on the map: the ground's height, whose ground, the thing there.
  bevel(w.lo, w.hi, false, kGui5);
  if (!terrain_ || windowAt(mouse_) || mouse_.x >= toolX()) return;
  const glm::vec2 at = toWorld(mouse_);
  const float ground = terrain_->rawHeight(at.x, at.y);
  // Whose: the nearer side's forces within 5 km.
  float ours = 5000.0f, theirs = 5000.0f, near = 6.0f * mpp_;
  std::string object = "No Object";
  if (targets_)
    for (const sim::Vehicle& v : targets_->list) {
      if (v.destroyed) continue;
      const float d = glm::distance(glm::vec2(v.pos.x, v.pos.z), at);
      if (v.side == sim::Side::Ours) ours = std::min(ours, d);
      if (v.side == sim::Side::Enemy) theirs = std::min(theirs, d);
      if (d < near) near = d, object = sim::vehicleType(v.kind).name;
    }
  const char* side = ours < theirs ? "Allied" : theirs < ours ? "Enemy" : "Neutral";
  const glm::vec2 o = w.lo + glm::vec2(5.0f, 22.0f);
  canvas_.text("Ground Height:", o, kWhite);
  canvas_.text(fmt("%.0f feet", double(std::max(0.0f, ground) / gamedata::kFeet)), o + glm::vec2(95.0f, 0.0f), kWhite);
  canvas_.text(std::string("Teritory: ") + side, o + glm::vec2(0.0f, 17.0f), kWhite);
  canvas_.text(object, o + glm::vec2(0.0f, 34.0f), kWhite);
}

// ---------------------------------------------------------------- the 3D view

namespace {
// The window's picture and its bar of buttons, from its corner (measured on the original).
// (The view fills the window but the bar; the window as large as the original's: 242 x 202.)
const glm::vec2 k3DViewLo{1.0f, 1.0f}, k3DViewMargin{1.0f, 20.0f};
constexpr float k3DBarUp = 19.0f, k3DButtonW = 42.0f, k3DButtonX = 2.0f;
constexpr float kFlySpeed = 120.0f;  // m/s the camera flies at
}  // namespace

void Planner::step3D(float dt) {
  if (mode3D_ == Mode3D::Fly) {
    // Turning towards the place and flying there, coming to a hover over it.
    const glm::vec2 d = target3D_ - cam3D_;
    const float dist = glm::length(d);
    if (dist > 1.0f) {
      const float want = std::atan2(d.x, -d.y);
      heading3D_ += std::clamp(std::remainder(want - heading3D_, 6.2831853f), -1.2f * dt, 1.2f * dt);
      const float step = std::min(dist, kFlySpeed * dt * std::clamp(dist / 300.0f, 0.15f, 1.0f));
      cam3D_ += glm::vec2(std::sin(heading3D_), -std::cos(heading3D_)) * step;
    }
  } else {
    orbit3D_ += 0.25f * dt;
    cam3D_ = target3D_;
  }
}

Planner::View3D Planner::view3D() const {
  View3D v;
  if (!isOpen(Screen::View3D) || !terrain_) return v;
  v.open = true;
  glm::vec2 size{240.0f, 181.0f};
  for (const Window& w : windows_)
    if (w.kind == Screen::View3D) size = (w.hi - k3DViewMargin) - (w.lo + k3DViewLo);
  v.size = glm::ivec2(glm::round(size * std::max(1.0f, canvas_.scale())));
  auto ground = [&](glm::vec2 p) { return terrain_->groundHeight(p.x, p.y); };
  const glm::vec3 target{target3D_.x, ground(target3D_) + 10.0f, target3D_.y};
  switch (mode3D_) {
    case Mode3D::Sat:
      // A picture straight down, north up.
      v.ortho = true;
      v.span = 2400.0f;
      v.eye = target + glm::vec3(0.0f, 3000.0f, 0.0f);
      v.at = target;
      v.up = {0.0f, 0.0f, -1.0f};
      break;
    case Mode3D::Point: {
      // Round the place, looking at it from 700 m out and 250 m up.
      const glm::vec2 e = target3D_ + glm::vec2(std::sin(orbit3D_), std::cos(orbit3D_)) * 700.0f;
      v.eye = {e.x, std::max(ground(e), target.y) + 250.0f, e.y};
      v.at = target;
      break;
    }
    case Mode3D::Fly: {
      const glm::vec2 fwd{std::sin(heading3D_), -std::cos(heading3D_)};
      v.eye = {cam3D_.x, ground(cam3D_) + 120.0f, cam3D_.y};
      v.at = v.eye + glm::vec3(fwd.x * 400.0f, -60.0f, fwd.y * 400.0f);
      break;
    }
  }
  return v;
}

void Planner::draw3D(const Window& w) {
  bevel(w.lo, w.hi, false, kGui5);
  const glm::vec2 lo = w.lo + k3DViewLo, hi = w.hi - k3DViewMargin;
  // The picture the game drew (its rows bottom up).
  if (view3DTex_.loaded()) canvas_.image(view3DTex_, lo, hi - lo, glm::vec4(0, 1, 1, 0));
  else canvas_.fill(lo, hi, kPanelDark);
  static const char* names[] = {"Sat", "Point", "Fly"};
  for (int i = 0; i < 3; ++i) {
    const glm::vec2 a = w.lo + glm::vec2(k3DButtonX + (k3DButtonW + 1.0f) * float(i), w.hi.y - w.lo.y - k3DBarUp), b = a + glm::vec2(k3DButtonW, 16.0f);
    const bool down = int(mode3D_) == i;
    bevel(a, b, down, kGui5);
    label(names[i], a + glm::vec2(down ? 1.0f : 0.0f), b + glm::vec2(down ? 1.0f : 0.0f));
  }
}

void Planner::click3D(const Window& w, glm::vec2 p) {
  for (int i = 0; i < 3; ++i) {
    const glm::vec2 a = w.lo + glm::vec2(k3DButtonX + (k3DButtonW + 1.0f) * float(i), w.hi.y - w.lo.y - k3DBarUp);
    if (inside(p, a, a + glm::vec2(k3DButtonW, 16.0f))) {
      mode3D_ = Mode3D(i);
      if (mode3D_ == Mode3D::Fly) cam3D_ = target3D_ + glm::vec2(0.0f, 600.0f), heading3D_ = 0.0f;
    }
  }
}

void Planner::drawBriefing(const Window& w) {
  // A window in the brick red like the others, the mission's name at its top, the text in
  // white, a page at a time: the facts and the orders (the other paragraphs of the file are
  // for the debriefing).
  bevel(w.lo, w.hi, false, kGui5);
  canvas_.clip(w.lo, w.hi);
  if (mission_) canvas_.text(mission_->title, w.lo + glm::vec2(20.0f, 5.0f), kWhite);
  const glm::vec2 lo = w.lo + glm::vec2(14.0f, 30.0f), hi = w.hi - glm::vec2(14.0f, 8.0f);
  const float width = hi.x - lo.x;
  std::vector<std::string> lines;
  if (mission_) {
    for (const auto& f : mission_->facts)
      for (const std::string& l : canvas_.wrap(f.first + ": " + f.second, width)) lines.push_back(l);
    if (!mission_->paragraphs.empty()) {
      lines.push_back("");
      for (const std::string& l : canvas_.wrap(mission_->paragraphs.front(), width)) lines.push_back(l);
    }
  }
  const int perPage = std::max(1, int((hi.y - lo.y - 24.0f) / canvas_.lineHeight()));
  pages_ = std::max(1, int((lines.size() + size_t(perPage) - 1) / size_t(perPage)));
  page_ = std::clamp(page_, 0, pages_ - 1);
  float y = lo.y;
  for (size_t i = size_t(page_ * perPage); i < lines.size() && i < size_t((page_ + 1) * perPage); ++i) {
    canvas_.text(lines[i], {lo.x, y}, kWhite);
    y += canvas_.lineHeight();
  }
  if (pages_ > 1) canvas_.text(fmt("Page %d/%d", page_ + 1, pages_), {hi.x - 10.0f, hi.y - 12}, kWhite, 1);
  canvas_.noClip();
}

void Planner::drawDebrief(const Window& w) {
  // As the original's: the title (the mission and its result) at the top, the lines below
  // (indented as given, long ones wrapped), a scroll bar down the left side under the close box,
  // its thumb as long as the part shown.
  bevel(w.lo, w.hi, false, kGui5);
  canvas_.clip(w.lo, w.hi);
  canvas_.text(debriefTitle_, w.lo + glm::vec2(20.0f, 5.0f), kWhite);
  const glm::vec2 lo = w.lo + glm::vec2(22.0f, 22.0f), hi = w.hi - glm::vec2(10.0f, 6.0f);
  std::vector<std::pair<float, std::string>> lines;  // indent, text
  for (const std::string& l : debriefText_) {
    const size_t start = std::min(l.find_first_not_of(' '), l.size());
    const float indent = canvas_.textWidth(std::string(start, ' '));
    const std::vector<std::string> wrapped = canvas_.wrap(l.substr(start), hi.x - lo.x - indent);
    if (wrapped.empty()) lines.push_back({0.0f, ""});
    for (const std::string& part : wrapped) lines.push_back({indent, part});
  }
  debriefTotal_ = int(lines.size());
  debriefFit_ = std::max(1, int((hi.y - lo.y) / canvas_.lineHeight()));
  scrollDebrief(0);
  float y = lo.y;
  for (int i = debriefTop_; i < debriefTotal_ && i < debriefTop_ + debriefFit_; ++i) {
    canvas_.text(lines[size_t(i)].second, {lo.x + lines[size_t(i)].first, y}, kWhite);
    y += canvas_.lineHeight();
  }
  scrollLo_ = w.lo + glm::vec2(1.0f, 17.0f), scrollHi_ = glm::vec2(w.lo.x + 15.0f, w.hi.y - 2.0f);
  bevel(scrollLo_, scrollHi_, true, kGui5);
  const float track = scrollHi_.y - scrollLo_.y - 2.0f, total = float(std::max(debriefTotal_, debriefFit_));
  thumbLo_ = scrollLo_.y + 1.0f + track * float(debriefTop_) / total;
  thumbHi_ = std::max(thumbLo_ + 6.0f, thumbLo_ + track * float(debriefFit_) / total);
  thumbHi_ = std::min(thumbHi_, scrollHi_.y - 1.0f);
  bevel({scrollLo_.x + 2.0f, thumbLo_}, {scrollHi_.x - 2.0f, thumbHi_}, false, kGui5);
  canvas_.noClip();
}

void Planner::debriefClick(glm::vec2 at) {
  // On the scroll bar: the thumb is taken hold of, above or below it a page goes by.
  if (!inside(at, scrollLo_, scrollHi_)) return;
  if (at.y < thumbLo_) scrollDebrief(-std::max(1, debriefFit_ - 1));
  else if (at.y > thumbHi_) scrollDebrief(std::max(1, debriefFit_ - 1));
  else scrollGrab_ = at.y - thumbLo_;
}

// ---------------------------------------------------------------- the map

glm::vec2 Planner::turn(glm::vec2 v, float a) {
  const float c = std::cos(a), s = std::sin(a);
  return {c * v.x - s * v.y, s * v.x + c * v.y};
}
glm::vec2 Planner::toMap(glm::vec2 world) const { return (kMainLo + screen_) * 0.5f + turn(world - centre_, rot_) / mpp_; }
glm::vec2 Planner::toWorld(glm::vec2 screen) const { return centre_ + turn(screen - (kMainLo + screen_) * 0.5f, -rot_) * mpp_; }

void Planner::fit() {
  // The whole route, north up.
  if (route_.empty()) return;
  rot_ = 0.0f;
  glm::vec2 lo = route_[0].pos, hi = lo;
  for (const PlanPoint& p : route_) lo = glm::min(lo, p.pos), hi = glm::max(hi, p.pos);
  // As the original's: in the middle of the whole screen (the tool box notwithstanding), over
  // two thirds of it (measured on its map of Anti-Artillery Attack: the route's 23.6 km over
  // 318 of its 480 pixels, centred at 320, 239).
  const glm::vec2 size = screen_ - kMainLo;
  mpp_ = std::max({(hi.x - lo.x) / size.x, (hi.y - lo.y) / size.y, 5.0f}) * 1.5f;
  centre_ = (lo + hi) * 0.5f;
}

void Planner::zoom(float factor, glm::vec2 about) {
  const glm::vec2 w = toWorld(about);
  mpp_ = std::clamp(mpp_ * factor, 4.0f, 600.0f);
  centre_ = w - turn(about - (kMainLo + screen_) * 0.5f, -rot_) * mpp_;
}

void Planner::back() { zoom(1.5f, (kMainLo + screen_) * 0.5f); }

void Planner::renderMap() {
  // As the original's: the ground's facets in the zone's light and dark sand (or water), with
  // Sides on dotted every fourth pixel blue, orange on the enemy's ground, the water and the roads, runways and towns, contours, rivers,
  // tracks. Pixel by pixel in its 640 x 480 screen.
  const int w = int(screen_.x - kMainLo.x), h = int(screen_.y - kMainLo.y);
  if (base_.width() != w || base_.height() != h) base_.resize(w, h);
  texCentre_ = centre_, texMpp_ = mpp_, texRot_ = rot_, texSides_ = showForces_;
  if (!map_.loaded()) {
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) base_.set(x, y, {219, 199, 163});
    return;
  }
  const glm::u8vec3 dots = map_.rgb(13), enemyDots = map_.rgb(14);
  const bool sides = showForces_;
  const bool enemyGround = sides && mission_ && !mission_->cellSides.empty();
  const float sp = map_.spacing;
  const int n = map_.size;
  // Screen pixel to zone posts: a step right and a step down.
  const glm::vec2 o = toWorld(kMainLo + glm::vec2(0.5f));
  const glm::vec2 dx = toWorld(kMainLo + glm::vec2(1.5f, 0.5f)) - o, dy = toWorld(kMainLo + glm::vec2(0.5f, 1.5f)) - o;
  auto post = [&](glm::vec2 world) { return glm::vec2(world.x + origin_.x, origin_.y - world.y) / sp; };
  const glm::vec2 p0 = post(o), px = post(o + dx) - p0, py = post(o + dy) - p0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const glm::vec2 g = p0 + px * float(x) + py * float(y);
      glm::u8vec3 c{0, 0, 0};
      if (g.x >= 0.0f && g.y >= 0.0f && g.x < float(n - 1) && g.y < float(n - 1)) {
        const int i = int(g.x), j = int(g.y);
        const bool first = gamedata::ZoneMap::firstTriangle(i, j, g.x - float(i), g.y - float(j));
        const uint8_t s = facet_[(size_t(i) * size_t(n) + size_t(j)) * 2 + (first ? 0 : 1)];
        if (!sides || ((x + (y & 1) * 2) & 3) != 0) c = map_.rgb(s == 255 ? 8 : s);
        else c = enemyGround && mission_->sideAt(g * sp) == 2 ? enemyDots : dots;
      }
      base_.set(x, y, c);
    }
  // The features of the cells on the screen.
  glm::vec2 lo{1e9f}, hi{-1e9f};
  for (const glm::vec2 corner : {kMainLo, glm::vec2(screen_.x, kMainLo.y), screen_, glm::vec2(kMainLo.x, screen_.y)}) {
    const glm::vec2 q = post(toWorld(corner)) * sp / map_.cellSize();
    lo = glm::min(lo, q), hi = glm::max(hi, q);
  }
  constexpr int C = gamedata::ZoneMap::kCells;
  const int cx0 = std::max(0, int(lo.x) - 1), cx1 = std::min(C - 1, int(hi.x) + 1);
  const int cy0 = std::max(0, int(lo.y) - 1), cy1 = std::min(C - 1, int(hi.y) + 1);
  auto screen = [&](glm::vec2 zone) { return toMap({zone.x - origin_.x, origin_.y - zone.y}) - kMainLo; };
  std::vector<glm::vec2> pts;
  // Contours every 250 ft close up (at the opening view of a mission, as the original's), further
  // out fewer (the original keeps them all, burying the map).
  float every = gamedata::ZoneMap::kContourFt;
  for (float m = 100.0f; mpp_ > m && every < 8000.0f; m *= 2.0f) every *= 4.0f;
  // Areas first (water, then the grey ones), the lines over them.
  for (int pass = 0; pass < 3; ++pass)
    for (int cx = cx0; cx <= cx1; ++cx)
      for (int cy = cy0; cy <= cy1; ++cy)
        for (const uint32_t k : map_.cells[size_t(cx * C + cy)]) {
          const gamedata::ZoneMap::Shape& s = map_.shapes[k];
          if (s.kind == gamedata::ZoneMap::Contour && (pass != 2 || std::fmod(s.level, every) != 0.0f)) continue;
          pts.clear();
          for (const glm::vec2& q : s.pts) pts.push_back(screen(q));
          if (pass == 0 && s.area && s.kind == gamedata::ZoneMap::Water) base_.polygon(pts, map_.rgb(8));
          if (pass == 1 && s.area && (s.kind == gamedata::ZoneMap::Road || s.kind == gamedata::ZoneMap::Other))
            base_.polygon(pts, map_.rgb(10));
          if (pass != 2 || s.area) continue;
          glm::u8vec3 c;
          switch (s.kind) {
            case gamedata::ZoneMap::Contour:
            case gamedata::ZoneMap::Ground: c = map_.rgb(7); break;
            case gamedata::ZoneMap::Water: c = map_.rgb(13); break;
            case gamedata::ZoneMap::Track: c = map_.rgb(9); break;
            default: c = map_.rgb(10); break;
          }
          for (size_t i = 0; i + 1 < pts.size(); ++i) base_.line(pts[i], pts[i + 1], c);
        }
}

int Planner::pointAt(glm::vec2 screen) const {
  int best = -1;
  float bestD = 8.0f;
  for (size_t i = 0; i < route_.size(); ++i) {
    const float d = glm::distance(toMap(route_[i].pos), screen);
    if (d < bestD) bestD = d, best = int(i);
  }
  return best;
}

void Planner::insertAfter(int i, glm::vec2 at) {
  if (i < 0 || i + 1 >= int(route_.size())) return;
  PlanPoint p;
  p.pos = at;
  p.source = route_[size_t(i) + 1].source;
  p.source.type = 2;
  p.source.wait = 0.0f;
  p.source.alt = 0.5f * (route_[size_t(i)].source.alt + route_[size_t(i) + 1].source.alt);
  route_.insert(route_.begin() + i + 1, p);
  selected_ = i + 1;
}

void Planner::erase(int i) {
  if (i < 0 || i >= int(route_.size()) || route_[size_t(i)].fixed || route_.size() <= 2) return;
  route_.erase(route_.begin() + i);
  selected_ = std::min(i, int(route_.size()) - 1);
}

void Planner::drawMap(float /*dt*/) {
  if (texMpp_ != mpp_ || texCentre_ != centre_ || texRot_ != rot_ || texSides_ != showForces_ || base_.width() != int(screen_.x) || base_.height() != int(screen_.y)) renderMap();
  raster_ = base_;
  Raster& r = raster_;
  const glm::u8vec3 white = map_.loaded() ? map_.rgb(15) : glm::u8vec3(255);
  const glm::u8vec3 blue = map_.loaded() ? map_.rgb(13) : glm::u8vec3(71, 131, 231);
  const glm::u8vec3 orange = map_.loaded() ? map_.rgb(14) : glm::u8vec3(255, 139, 35);
  const glm::u8vec3 gridC = map_.loaded() ? map_.rgb(11) : glm::u8vec3(67, 67, 131);
  const glm::u8vec3 brick{113, 56, 56};
  auto at = [&](glm::vec2 world) { return toMap(world) - kMainLo; };
  // Grid lines on the zone's own coordinates, a kilometre apart close up, further out wider.
  if (showGrid_) {
    float g = 1000.0f;
    for (const float step : {2000.0f, 5000.0f, 10000.0f, 20000.0f, 50000.0f})
      if (g / mpp_ < 40.0f) g = step;
    glm::vec2 lo{1e9f}, hi{-1e9f};
    for (const glm::vec2 corner : {kMainLo, glm::vec2(screen_.x, kMainLo.y), screen_, glm::vec2(kMainLo.x, screen_.y)}) {
      const glm::vec2 w = toWorld(corner), zone{w.x + origin_.x, origin_.y - w.y};
      lo = glm::min(lo, zone), hi = glm::max(hi, zone);
    }
    auto world = [&](float x, float y) { return glm::vec2(x - origin_.x, origin_.y - y); };
    for (float x = std::ceil(lo.x / g) * g; x <= hi.x; x += g) r.line(at(world(x, lo.y)), at(world(x, hi.y)), gridC);
    for (float y = std::ceil(lo.y / g) * g; y <= hi.y; y += g) r.line(at(world(lo.x, y)), at(world(hi.x, y)), gridC);
  }
  // The air defences' reach as the original's map key has them: rings round the mission's
  // ":Gameplay AAA" and "SAM" points (where the game puts its guns and missiles), blue on our
  // ground and orange on the enemy's; every gun 2 nautical miles, every missile 5, whatever it is
  // (measured on its map of Anti-Artillery Attack: 50 and 126 pixels at 74 m a pixel).
  if (mission_) {
    for (int kind = 0; kind < 2; ++kind)
      for (const glm::vec2 z : kind == 0 ? mission_->gameplayAaa : mission_->gameplaySam) {
        const int side = mission_->sideAt(z);
        const bool ours = side == 1;
        if (side != 1 && side != 2) continue;
        if (!(kind == 0 ? (ours ? alliedAaa_ : enemyAaa_) : (ours ? alliedSam_ : enemySam_))) continue;
        const float reach = (kind == 0 ? 2.0f : 5.0f) * 1852.0f;
        r.circle(at({z.x - origin_.x, origin_.y - z.y}), reach / mpp_, ours ? blue : orange);
      }
  }
  // The compass in the bottom right corner of the map, turned with it.
  const glm::vec2 compass{toolX() - 28.0f, screen_.y - 70.0f};
  if (showCompass_) {
    r.circle(compass, 15.0f, white);
    r.line(compass + turn({-19, 0}, rot_), compass + turn({19, 0}, rot_), white);
    r.line(compass + turn({0, 19}, rot_), compass + turn({0, -60}, rot_), white);
  }
  // The route as the mission gave it in brick red under the player's (where a waypoint was
  // moved, its trace stays), the flight chosen in the task box orange; the player's route in
  // white, its waypoints ringed (targets in a triangle), the one chosen filled blue, the
  // departure in a square.
  if (showRoute_) {
    auto marker = [&](glm::vec2 p, bool target, glm::u8vec3 c) {
      if (!target) return r.circle(p, 7.0f, c);
      const glm::vec2 a = p + glm::vec2(0.0f, -10.0f), b = p + glm::vec2(-9.0f, 6.0f), d = p + glm::vec2(9.0f, 6.0f);
      r.line(a, b, c), r.line(b, d, c), r.line(d, a, c);
    };
    if (const gamedata::MissionDrone* d = mission_ ? mission_->player() : nullptr)
      for (size_t i = 0; i < d->route.size(); ++i) {
        const glm::vec2 p = at({d->route[i].pos.x - origin_.x, origin_.y - d->route[i].pos.y});
        if (i + 1 < d->route.size())
          r.line(p, at({d->route[i + 1].pos.x - origin_.x, origin_.y - d->route[i + 1].pos.y}), brick);
        marker(p, d->route[i].type == 4, brick);
      }
    const std::vector<Flight> f = flights();
    if (flight_ > 0 && size_t(flight_) < f.size())
      for (size_t i = 0; i < f[size_t(flight_)].route.size(); ++i) {
        const std::vector<glm::vec2>& fr = f[size_t(flight_)].route;
        if (i + 1 < fr.size()) r.line(at(fr[i]), at(fr[i + 1]), orange);
        r.circle(at(fr[i]), 7.0f, orange);
      }
    for (size_t i = 0; i + 1 < route_.size(); ++i) r.line(at(route_[i].pos), at(route_[i + 1].pos), white);
    for (size_t i = 0; i < route_.size(); ++i) {
      const glm::vec2 p = at(route_[i].pos);
      const bool target = route_[i].source.type == 4;
      if (int(i) == selected_ && !target) r.disc(p, 7.0f, blue);
      marker(p, target, white);
      if (i == 0) r.rect(glm::ivec2(p) - glm::ivec2(9), glm::ivec2(p) + glm::ivec2(9), white);
    }
  }
  // Zoom: the rectangle being drawn, a line of grey.
  if (boxing_) {
    const glm::vec2 h = zoomBoxHalf();
    r.rect(glm::ivec2(pressAt_ - h - kMainLo), glm::ivec2(pressAt_ + h - kMainLo), map_.loaded() ? map_.rgb(10) : glm::u8vec3(115));
  }
  mapTex_.uploadRgb(r.width(), r.height(), r.pixels());
  canvas_.image(mapTex_, kMainLo, screen_ - kMainLo);

  if (showCompass_) canvas_.text("N", compass + turn({0, -70}, rot_) - glm::vec2(0, 4), kWhite, 0);
  // Where the 3D view's camera is: "3D" in grey.
  if (isOpen(Screen::View3D)) {
    const glm::vec2 p = toMap(mode3D_ == Mode3D::Fly ? cam3D_ : target3D_);
    canvas_.text("3D", p - glm::vec2(0.0f, 4.0f), glm::vec4(0.75f, 0.75f, 0.75f, 1.0f), 0);
  }
  if (!showRoute_) return;
  // The waypoints' letters in their rings; points on top of each other (the landing back at
  // the departure) show the first.
  for (size_t i = 0; i < route_.size(); ++i) {
    size_t j = 0;
    while (j < i && glm::distance(toMap(route_[j].pos), toMap(route_[i].pos)) > 6.0f) ++j;
    if (j == i) canvas_.text(std::string(1, letter(i)), toMap(route_[i].pos) - glm::vec2(0.0f, route_[i].source.type == 4 ? 2.0f : 4.0f), kWhite, 0);
  }
}

glm::vec2 Planner::zoomBoxHalf() const {
  // Round where the drag began, a corner at the mouse, as wide for its height as the screen.
  const float aspect = screen_.x / screen_.y;
  const glm::vec2 d = glm::abs(mouse_ - pressAt_);
  const float hx = std::max(d.x, d.y * aspect);
  return {hx, hx / aspect};
}

glm::vec2 Planner::menuLo() const {
  // Centred on where it was clicked, kept on the payload picture.
  const float h = 2.0f + kMenuItemH * float(menuItems());
  glm::vec2 lo = menuAt_ - glm::vec2(kMenuW * 0.5f, h * 0.5f);
  return glm::clamp(lo, kPictureLo, glm::vec2(kPictureHi.x - kMenuW, kPictureHi.y - h));
}

bool Planner::payloadClick(glm::vec2 pic) {
  // A pylon pair to load, or a store laid out on the ground: the menu of the pylons it fits.
  // As in the original, a click on what hangs there takes it off (the helicopter can be
  // disarmed all but the turret gun).
  for (int p = 0; p < sim::kPylons; ++p) {
    const Spot r = mirrored(kPylonSpot[p]);
    if (inside(pic, kPylonSpot[p].lo, kPylonSpot[p].hi) || inside(pic, r.lo, r.hi)) {
      pylon_ = sim::Pylon(p);
      door_ = -1;
      note_.clear();
      loadout_.at(sim::Pylon(p)) = sim::Store::None;
      return true;
    }
  }
  // A door, the door gun on the concrete: the menu of the doors; a click on a door gun in its
  // window takes it in.
  for (int d = 0; d < 2; ++d) {
    const Spot ds = d == 0 ? kDoorSpot : mirrored(kDoorSpot);
    if (inside(pic, ds.lo, ds.hi)) {
      door_ = d;
      note_.clear();
      loadout_.doorGun[d] = false;
      return true;
    }
  }
  if (inside(pic, kGshgSpot.lo, kGshgSpot.hi) || inside(pic, mirrored(kGshgSpot).lo, mirrored(kGshgSpot).hi)) {
    menuDoors_ = true, menuAt_ = mouse_, note_.clear();
    return true;
  }
  for (const GroundStore& g : kGround) {
    const Spot r = mirrored(g.spot);
    if (!inside(pic, g.spot.lo, g.spot.hi) && !inside(pic, r.lo, r.hi)) continue;
    const sim::StoreInfo& si = sim::storeInfo(g.store);
    if (!si.available) {
      note_ = std::string(si.name) + " is not in service yet";
      return true;
    }
    menuPylons_.clear();
    for (int p = 0; p < sim::kPylons; ++p)
      if (sim::storeFits(g.store, sim::Pylon(p))) menuPylons_.push_back(sim::Pylon(p));
    if (!menuPylons_.empty()) menuStore_ = g.store, menuAt_ = mouse_, note_.clear();
    return true;
  }
  return false;
}

void Planner::drawPayload(const Window& w) {
  const glm::vec2 o = w.lo + glm::vec2(2.0f);  // the picture's corner, in the window's frame
  canvas_.clip(o, w.hi);
  if (payload_.loaded()) canvas_.image(payload_, o);
  else canvas_.fill(o, w.hi, kPanelDark);
  // What hangs on the pylons.
  for (const Sprite& s : kSprites) {
    if (loadout_.at(s.pylon) != s.store) continue;
    canvas_.image(sprites_[int(s.store)][int(s.pylon)][0], o + s.at);
    canvas_.image(sprites_[int(s.store)][int(s.pylon)][1], o + s.atRight);
  }
  // The door guns: on the concrete, and out of the windows they are put in.
  const auto mirroredAt = [](glm::vec2 at, const render::Texture& t) { return glm::vec2(544.0f - at.x - float(t.width), at.y); };
  canvas_.image(gshgGround_[0], o + kGshgGroundAt);
  canvas_.image(gshgGround_[1], o + mirroredAt(kGshgGroundAt, gshgGround_[1]));
  // The interceptor drones' racks: on the concrete, and under the wings when loaded.
  canvas_.image(interceptorGround_, o + kInterceptorGroundAt);
  canvas_.image(interceptorGround_, o + mirroredAt(kInterceptorGroundAt, interceptorGround_));
  if (loadout_.inner == sim::Store::Interceptor) {
    for (int side = 0; side < 2; ++side) {
      const glm::vec2 st = kInterceptorStruts[side];
      canvas_.fill(o + st, o + glm::vec2(st.x + 1.0f, kInterceptorPylonAt[side].y), kInterceptorStrut);
      canvas_.image(interceptorPylon_, o + kInterceptorPylonAt[side]);
    }
  }
  for (int d = 0; d < 2; ++d)
    if (loadout_.doorGun[d]) canvas_.image(gshgWindow_[d], o + kGshgWindowAt[d]);
  // The pylon pair being loaded (or the door), the store under the mouse.
  if (door_ >= 0) {
    const Spot ds = door_ == 0 ? kDoorSpot : mirrored(kDoorSpot);
    canvas_.frame(o + ds.lo, o + ds.hi, kAmber);
  } else {
    const Spot ps = kPylonSpot[int(pylon_)];
    canvas_.frame(o + ps.lo, o + ps.hi, kAmber);
    canvas_.frame(o + mirrored(ps).lo, o + mirrored(ps).hi, kAmber);
  }
  static const char* kGshgTitle = "GSHG-7.62 DOOR GUN AND GUNNER";
  std::string hover;
  const glm::vec2 pic = mouse_ - o;
  for (const GroundStore& g : kGround) {
    const Spot r = mirrored(g.spot);
    if (inside(pic, g.spot.lo, g.spot.hi) || inside(pic, r.lo, r.hi)) hover = sim::storeInfo(g.store).title;
  }
  if (inside(pic, kGshgSpot.lo, kGshgSpot.hi) || inside(pic, mirrored(kGshgSpot).lo, mirrored(kGshgSpot).hi)) hover = kGshgTitle;
  if (menuStore_ != sim::Store::None) hover = sim::storeInfo(menuStore_).title;
  if (menuDoors_) hover = kGshgTitle;
  // The bar: what is under the mouse, else what the chosen pylons (the door) carry.
  const sim::StoreInfo& si = sim::storeInfo(loadout_.at(pylon_));
  const std::string chosen = door_ >= 0 ? (loadout_.doorGun[door_] ? std::string(door_ == 0 ? "GSHG-7.62 IN THE LEFT DOOR" : "GSHG-7.62 IN THE RIGHT DOOR")
                                                                    : std::string("no door gun"))
                           : loadout_.at(pylon_) == sim::Store::None ? std::string("no weapon selected")
                                                                     : std::string(si.title);
  std::string bar = !note_.empty() ? note_ : !hover.empty() ? hover : chosen;
  // A longer text widens the bar about its middle up to the picture's width, and then it grows
  // upwards a line at a time.
  const float pad = 8.0f, mid = (kBarLo.x + kBarHi.x) * 0.5f, lh = canvas_.lineHeight();
  const float barW = std::clamp(canvas_.textWidth(bar) + 2.0f * pad, kBarHi.x - kBarLo.x, 528.0f);
  std::vector<std::string> lines = canvas_.wrap(bar, barW - 2.0f * pad);
  if (lines.empty()) lines.emplace_back();
  const glm::vec2 barLo{mid - barW * 0.5f, kBarLo.y - lh * float(lines.size() - 1)}, barHi{mid + barW * 0.5f, kBarHi.y};
  canvas_.fill(o + barLo, o + barHi, kGui5);
  canvas_.clip(o + barLo, o + barHi);
  for (size_t i = 0; i < lines.size(); ++i) canvas_.text(lines[i], o + barLo + glm::vec2(pad, 7.0f + lh * float(i)), kWhite);
  canvas_.noClip();
  if (menuOpen()) {
    // The pylon menu: a raised box, the item under the mouse raised in it.
    static const char* names[sim::kPylons] = {"Inner Pylon", "Outer Pylon", "Wingtip Pylon"};
    const glm::vec2 lo = menuLo(), hi = lo + glm::vec2(kMenuW, 2.0f + kMenuItemH * float(menuItems()));
    bevel(lo, hi, false, kGui5);
    for (size_t i = 0; i < menuItems(); ++i) {
      const glm::vec2 a = lo + glm::vec2(1.0f, 1.0f + kMenuItemH * float(i)), b = a + glm::vec2(kMenuW - 2.0f, kMenuItemH - 1.0f);
      if (inside(mouse_, a, b)) bevel(a, b, false, kGui5);
      // The interceptor drones' rack hangs from the inner pylon and takes the outer one too.
      const char* item = menuDoors_ ? kDoorName[i] : menuStore_ == sim::Store::Interceptor ? "Both Pylons" : names[int(menuPylons_[i])];
      canvas_.text(item, a + glm::vec2(4.0f, 6.0f), kWhite);
    }
  }
}

void Planner::choose(sim::Store s) {
  // A store that does not fit the pylons chosen goes where it fits (the outmost such pylons:
  // 9M114 to the wingtips).
  std::string why;
  if (!sim::storeFits(s, pylon_, &why)) {
    int p = sim::kPylons - 1;
    while (p >= 0 && !sim::storeFits(s, sim::Pylon(p))) --p;
    if (p < 0) {
      note_ = why;
      return;
    }
    pylon_ = sim::Pylon(p);
  }
  if (!sim::storeInfo(s).available) {
    note_ = std::string(sim::storeInfo(s).name) + " IS NOT IN SERVICE YET";
    return;
  }
  if (pylon_ == sim::Pylon::Outer && s != sim::Store::None && loadout_.outerTaken()) {
    note_ = "THE INTERCEPTOR RACK TAKES THE OUTER PYLONS TOO";
    return;
  }
  loadout_.at(pylon_) = s;
  loadout_.settle();  // the interceptor rack clears the outer pylons
  note_.clear();
}

}  // namespace menu
