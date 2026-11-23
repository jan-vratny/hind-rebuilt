#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include <SDL.h>
#include <glm/glm.hpp>

#include "gamedata/mission.hpp"
#include "gamedata/zone.hpp"
#include "gamedata/zonemap.hpp"
#include "menu/raster.hpp"
#include "render/canvas.hpp"
#include "sim/targets.hpp"
#include "sim/weapons.hpp"
#include "world/terrain.hpp"

// Mission planning before take-off, as the original's: the map of the zone with the route always
// fills the screen, the "Map Tool Box" on the right (Briefing, Takeoff, the map's tools, Exit),
// and what a button opens comes up in a window over the map (the briefing, the payload, the
// waypoint, the weather...), closed with the box at its top left corner. Buttons, windows and
// menus in the original's brick red (the palette of its UIDATA, whose close box is drawn as it
// is), its font, its pictures (MENUSCRN). Drawn in its 640 x 480 screen; everything can be done
// with the keyboard as well as the mouse.
namespace menu {

// The windows over the map: the briefing, the payload; the waypoints (their letters) and the
// chosen one's information; the map key (layers on and off), the weather, the flights of the
// task, the information about the point under the mouse; the 3D view of the place clicked on
// the map (Sat straight down, Point circling round it, Fly flying there; "3D" on the map where
// its camera is).
// Debrief: after the mission (Planner::debrief), its result in the window's title.
enum class Screen { Briefing, Payload, Way, WayInfo, Key, Met, Task, Point, View3D, Debrief };
// LogYes, LogRetry, LogNo: the answer to "Do you wish to log this mission?" (Planner::askLog).
enum class Result { None, TakeOff, Quit, LogYes, LogRetry, LogNo };

// A point of the player's route, world x, z.
struct PlanPoint {
  glm::vec2 pos{0.0f};
  gamedata::MissionWaypoint source;  // as in the mission (a new point: a turning point)
  bool fixed = false;                // departure and landing stay where they are
  std::string what;                  // a target's name (sim::waypointTargets)
};

class Planner {
public:
  // Pictures from the original game in `originalDir`; `zone` such as "afghan".
  bool init(const std::string& originalDir, const std::string& zone);
  // A mission to plan: the zone point `origin` is the world origin, `targets` its forces.
  // `targetNames`: what the route's target waypoints are (sim::waypointTargets).
  void start(const gamedata::Mission& mission, glm::vec2 origin, const world::Terrain& terrain,
             const sim::Targets& targets, const sim::Loadout& loadout,
             const std::vector<std::string>& targetNames = {});

  // After the mission, as the original's: the map with the route, only the Debrief button (the
  // debriefing's window open and shut) and Exit; the window's title the mission's and its result
  // ("Shipping Attack - ABORTED"), its paragraphs below. Exit (Enter, Esc) gives Result::Quit.
  void debrief(const std::string& title, const std::vector<std::string>& paragraphs);
  bool debriefing() const { return debrief_; }
  // Leaving the debriefing of a single mission, as the original asks: "Do you wish to log this
  // mission?" Yes (Y, Enter), Retry (R: fly it again at once) or No (N, Esc).
  void askLog() { askLog_ = true; }

  // Window pixels to drawable pixels (high-DPI windows), for the mouse.
  void setMouseScale(glm::vec2 s) { mouseScale_ = s; }
  // Window pixels to one of the screen's at any size of the window (0: 640 x 480 fitted).
  void setUiScale(int s) { uiScale_ = s; }
  Result handleEvent(const SDL_Event& e);
  // A point of the 640 x 480 screen in window pixels (after the first draw), for scripted clicks.
  glm::vec2 windowPoint(glm::vec2 screen) const;
  void draw(int w, int h, float dt);

  // Opens a window (or brings it to the front), closes one; whether one is open.
  void open(Screen s);
  void close(Screen s);
  bool isOpen(Screen s) const;
  const sim::Loadout& loadout() const { return loadout_; }

  // The 3D window's camera, for the game to draw the world with into view3DTarget() (the planner
  // does not draw the world itself): eye and the point looked at, world axes; ortho: straight
  // down, `span` metres across.
  struct View3D {
    bool open = false;
    glm::vec3 eye{0.0f}, at{0.0f}, up{0.0f, 1.0f, 0.0f};
    bool ortho = false;
    float span = 0.0f;
    glm::ivec2 size{0};  // pixels to draw
  };
  View3D view3D() const;
  render::Texture& view3DTarget() { return view3DTex_; }
  // The edited route of the player's helicopter put back into the mission.
  void applyRoute(gamedata::Mission& mission) const;
  const std::vector<PlanPoint>& route() const { return route_; }

private:
  struct Button {
    std::string label;
    glm::vec2 lo, hi;  // pixels lo .. hi - 1
    int id;
    bool down = false;  // a toggle that is on
  };
  struct Window {
    Screen kind;
    glm::vec2 lo, hi;  // pixels lo .. hi - 1; the close box is at lo
  };
  std::vector<Button> buttons() const;
  Result press(int id);
  void toggle(Screen s);
  Window* windowAt(glm::vec2 p);
  void drawToolBox();
  void drawWindow(const Window& w);
  void drawBriefing(const Window& w);
  void drawPayload(const Window& w);
  void drawWay(const Window& w);
  void drawWayInfo(const Window& w);
  void drawKey(const Window& w);
  void drawMet(const Window& w);
  void drawTask(const Window& w);
  void drawPoint(const Window& w);
  void draw3D(const Window& w);
  void drawDebrief(const Window& w);
  void click3D(const Window& w, glm::vec2 p);
  void step3D(float dt);
  // Clicks in the windows (screen coordinates); right: the right button.
  void wayClick(const Window& w, glm::vec2 p);
  void wayButton(int k);  // Ins, Del, Info
  // A waypoint's letter: targets X, Y, Z, the others A, B, C... (as the original names them).
  char letter(size_t i) const;
  void wayInfoClick(const Window& w, glm::vec2 p, bool right);
  void keyClick(const Window& w, glm::vec2 p);
  void taskClick(const Window& w, glm::vec2 p);
  // A row of digit boxes (as the original's: pressed in) showing `value`; the one at `p` or -1.
  void digits(glm::vec2 at, int count, long value);
  int digitAt(glm::vec2 at, int count, glm::vec2 p) const;
  // The flights of the player's task (the player first, then the wingmen): name and route.
  struct Flight {
    std::string name;
    std::vector<glm::vec2> route;
  };
  std::vector<Flight> flights() const;

  // The original's look: a raised (or pressed) box in the brick red, white text.
  void bevel(glm::vec2 lo, glm::vec2 hi, bool pressed, glm::vec4 face);
  void label(const std::string& s, glm::vec2 lo, glm::vec2 hi);

  // Map: world x, z to the screen and back (turned by rot_).
  static glm::vec2 turn(glm::vec2 v, float a);
  glm::vec2 toMap(glm::vec2 world) const;
  glm::vec2 toWorld(glm::vec2 screen) const;
  void fit();
  void zoom(float factor, glm::vec2 about);
  void back();
  void renderMap();
  void drawMap(float dt);
  int pointAt(glm::vec2 screen) const;
  void insertAfter(int i, glm::vec2 at);
  void erase(int i);

  // Payload (in its window's picture coordinates).
  void choose(sim::Store s);
  bool payloadClick(glm::vec2 pic);

  render::Canvas canvas_;
  bool ready_ = false;
  render::Texture payload_, mapTex_, closeOff_, closeOn_;
  render::Texture sprites_[sim::kStores][sim::kPylons][2];

  const gamedata::Mission* mission_ = nullptr;
  const world::Terrain* terrain_ = nullptr;
  const sim::Targets* targets_ = nullptr;
  glm::vec2 origin_{0.0f};
  std::vector<PlanPoint> route_, saved_;
  std::vector<std::string> targetNames_;
  void missionRoute();  // route_ as the mission has it
  sim::Loadout loadout_;

  std::vector<Window> windows_;  // the front one last
  // The screen: 640 x 480 as the original's, or larger (the window at a fixed scale); the tool
  // box's left edge. ui scale: window pixels to a screen pixel (0: the screen fitted).
  glm::vec2 screen_{640.0f, 480.0f};
  bool fitPending_ = false;  // the opening view (fit) still to be set
  float toolX() const { return screen_.x - 83.0f; }
  int uiScale_ = 0;
  glm::vec2 mouseScale_{1.0f}, mouse_{0.0f};
  int held_ = -1;                // the button the mouse holds down
  int closing_ = -1;             // the window whose close box the mouse holds down
  // A window moved by its title bar, or made larger or smaller by its bottom right corner (the
  // RESIZE.BM grip); where the mouse took hold of it.
  int moving_ = -1, sizing_ = -1;
  glm::vec2 grab_{0.0f};
  static bool resizable(Screen s) { return s != Screen::Payload; }
  // The smallest a window can be made.
  static glm::vec2 minSize(Screen s);
  int page_ = 0, pages_ = 1;
  // Map view: the world point in the middle, metres per screen pixel, the turn (Rotate, rad
  // clockwise); the zone's map, the shade of each post's two triangles (192 + it; 255 water); the
  // map drawn for the view, with what moves on it.
  glm::vec2 centre_{0.0f};
  float mpp_ = 50.0f, rot_ = 0.0f;
  gamedata::ZoneMap map_;
  std::vector<uint8_t> facet_;
  Raster base_, raster_;
  glm::vec2 texCentre_{0.0f};
  float texMpp_ = 0.0f, texRot_ = 0.0f;
  bool texSides_ = false;  // the map drawn with the sides' ground (Sides)
  // Rotate: the mouse turns the map round the middle of the screen; the angle of the mouse
  // and the turn when it took hold.
  bool turning_ = false;
  // What a click or a drag on the map does, as the tool box's Zoom, Rotate, Way and Disable set
  // it: a rectangle to zoom into, turning the map, putting the waypoint chosen in the Way window
  // there, nothing.
  enum class MapMode { Zoom, Rotate, Way, Disable } mode_ = MapMode::Disable;
  bool boxing_ = false;  // Zoom: the rectangle being drawn, round pressAt_
  float grabAngle_ = 0.0f, grabRot_ = 0.0f;
  int selected_ = -1, dragging_ = -1;
  bool panning_ = false;
  // Map layers (Key) and the forces (Disable hides them).
  bool showForces_ = true, showGrid_ = true, showRoute_ = true, showCompass_ = true;
  bool alliedAaa_ = true, alliedSam_ = true, enemyAaa_ = true, enemySam_ = true;
  int flight_ = 0;  // the flight chosen in the task box
  // The 3D view: its mode, the place looked at and the camera (world x, z; heading).
  enum class Mode3D { Sat, Point, Fly } mode3D_ = Mode3D::Fly;
  glm::vec2 target3D_{0.0f}, cam3D_{0.0f};
  float heading3D_ = 0.0f, orbit3D_ = 0.0f;
  render::Texture view3DTex_, resize_;
  glm::vec2 pressAt_{0.0f};  // where the left button went down on the map
  bool pressed3D_ = false;
  glm::vec2 zoomBoxHalf() const;
  std::string status_;  // what the last button said (a tool not there yet)
  // Payload: the pylon pair being loaded, the message for its bar.
  sim::Pylon pylon_ = sim::Pylon::Inner;
  std::string note_;
  // The menu of pylons a clicked store can hang on (as in the original: centred on the mouse),
  // or of the doors for the GShG-7.62 (menuDoors_).
  sim::Store menuStore_ = sim::Store::None;
  bool menuDoors_ = false;
  glm::vec2 menuAt_{0.0f};
  std::vector<sim::Pylon> menuPylons_;
  bool menuOpen() const { return menuStore_ != sim::Store::None || menuDoors_; }
  void closeMenu() { menuStore_ = sim::Store::None, menuDoors_ = false; }
  size_t menuItems() const { return menuDoors_ ? 3 : menuPylons_.size(); }
  glm::vec2 menuLo() const;
  // The door chosen instead of a pylon pair (0 left, 1 right; -1 none); the GShG-7.62 lying on
  // the concrete and put out of the windows (a picture of our own: the original has none).
  int door_ = -1;
  bool debrief_ = false;
  bool askLog_ = false;
  std::string debriefTitle_;
  // The debriefing's lines as wrapped to its window, the first shown, how many fit; the scroll
  // bar's thumb held (the mouse's offset into it), and where the bar and its thumb were drawn.
  int debriefTop_ = 0, debriefFit_ = 1, debriefTotal_ = 0;
  float scrollGrab_ = -1.0f;
  glm::vec2 scrollLo_{0.0f}, scrollHi_{0.0f};
  float thumbLo_ = 0.0f, thumbHi_ = 0.0f;
  void scrollDebrief(int lines) { debriefTop_ = std::clamp(debriefTop_ + lines, 0, std::max(0, debriefTotal_ - debriefFit_)); }
  void debriefClick(glm::vec2 at);
  std::vector<std::string> debriefText_;
  render::Texture gshgGround_[2], gshgWindow_[2];
  render::Texture interceptorGround_, interceptorPylon_;  // drawn here (menu/planner.cpp)
};

}  // namespace menu
