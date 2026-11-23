#pragma once
#include <functional>
#include <string>
#include <vector>

#include <SDL.h>
#include <glm/glm.hpp>

#include "gamedata/anim.hpp"
#include "gamedata/mission.hpp"
#include "net/link.hpp"
#include "render/canvas.hpp"
#include "gamedata/campaign.hpp"
#include "platform/config.hpp"
#include "platform/joystick.hpp"
#include "render/recognition.hpp"
#include "sim/pilot_log.hpp"
#include "sim/topten.hpp"

// The original game's menus before the flying: the title menu (Quickstart, Flight, Preferences,
// Pilot's Log), its Quickstart and Flight menus, the choice of the war, the lists of training and
// single missions with their briefings, and the question whether to quit. The pictures are the
// original's (MENUSCRN): the stone buttons are part of the backgrounds, a pressed button is a
// brush of its own laid over it. Drawn in the 640 x 480 screen; mouse and keyboard (arrows,
// Enter, Escape back, a button's initial letter).
namespace menu {

// Log: the Pilot's Log; Award: a promotion's or a medal's picture after a mission logged;
// Network: the network game's (the games on the local network, hosting one, joining one);
// Recognition: the library of the units, a turning model each; Prefs: our settings (hind.ini).
enum class Page { Main, Quick, Flight, World, Missions, Exit, Log, Award, Network, Recognition, Prefs, TopTen, Campaign, Animation };

// What to fly, as chosen.
struct Choice {
  std::string mission = "campaign";  // e.g. "SARATT01", or the zone's campaign
  std::string zone = "afghan";       // afghan, kazak, korea, saratov
  bool plan = true;                  // through the planning screens (Quickstart takes off at once)
  bool arcade = false;               // the easy flight model
  bool infiniteWeapons = false, noCrash = false, wsoAllies = false;
  bool gunnerHints = true;           // the door gunners' shot on the HUD (remembered in hind.ini)
  bool ukraineInterceptors = false;  // Quickstart over Ukraine: the interceptor rack, not R-60s (hind.ini)
};

// Seats of a network game: a Hind of one's own in the game, or in a pilot's crew.
// Bowman: the hidden one (Ctrl+Shift+R on the page), the lone bowman on foot (sim::Bowman).
enum class NetSeat { Pilot, Gunner, LeftDoor, RightDoor, Bowman };
const char* netSeatName(NetSeat s);
// The view (main's --view) the seat starts in: wingman, gunner, door-left, door-right, bowman.
const char* netSeatView(NetSeat s);

// The network game as chosen: the player's name, the game's name (hosting), where to join.
struct NetChoice {
  std::string name, game;
  std::string address;  // host or host:port
  std::string joinGame; // the name of the game joined
  NetSeat seat = NetSeat::Gunner;
};

// Host: open the game (then the missions are chosen as usual); StopHost: close it; Join: join
// the game chosen (NetChoice).
enum class FrontResult { None, Fly, Quit, Host, StopHost, Join };

class FrontEnd {
public:
  bool init(const std::string& originalDir);
  // The "Gunner hints" toggle as hind.ini has it.
  void setGunnerHints(bool on) { gunnerHints_ = on; }
  // The "Ukraine:" toggle of Quickstart's choice of the war, as hind.ini has it ([weapons] quick_ukraine).
  void setUkraineInterceptors(bool on) { ukraineInterceptors_ = on; }
  // The Pilot's Log, kept in `path` (saved when changed here).
  void setLog(sim::PilotLog* log, const std::string& path) { log_ = log, logPath_ = path; }
  // What a mission logged brought (a promotion, medals): their pictures, shown before the menus.
  void showAwards(const sim::LogOutcome& o);
  void open(Page p);
  // A page by name, for the development options: main, quick, flight, training, single,
  // campaign (the choice of the war), exit, network.
  bool openByName(const std::string& name);

  void setMouseScale(glm::vec2 s) { mouseScale_ = s; }
  // A point of the 640 x 480 screen in window pixels (after the first draw), for scripted clicks.
  glm::vec2 windowPoint(glm::vec2 screen) const;
  FrontResult handleEvent(const SDL_Event& e);
  // Time passes: a button pressed from the keyboard shows pressed for a moment, then acts.
  FrontResult update(float dt);
  void draw(int w, int h);

  const Choice& choice() const { return choice_; }
  Page page() const { return page_; }

  // The network game: the names as remembered, the games heard on the network, whether this
  // game is hosting one (and who is in it, a line for the menus).
  void setNetNames(const std::string& name, const std::string& game) { net_.name = name, net_.game = game; }
  void setNetGames(const std::vector<net::Advert>& games) { games_ = games; }
  void setHosting(bool on, const std::string& status) { hosting_ = on, hostStatus_ = status; }
  const NetChoice& netChoice() const { return net_; }
  void note(const std::string& text) { note_ = text, noteTime_ = 3.0f; }
  // The preferences page's settings: the configuration they change (and save to `path`), told
  // to `changed` (section, key) for what takes effect at once (the window, the sound).
  void setConfig(platform::Config* cfg, const std::string& path, std::function<void(const std::string&, const std::string&)> changed) {
    cfg_ = cfg, cfgPath_ = path, changed_ = std::move(changed);
  }
  // The Top Ten of Quickstart Arcade (saved to `path`); a flight's score: its blank line for the
  // pilot's name on the Top Ten page when it is high enough (false: it is not).
  void setTopTen(sim::TopTen* t, const std::string& path) { topTen_ = t, topTenPath_ = path; }
  bool enterTopTen(int score, bool crashed = false);
  // The campaign's page (C_AFGHAN.LBM ...): how the war's campaign stands, read out; then on to
  // its next mission (FrontResult::Fly), or, the campaign over, back to the wars.
  void openCampaign(const std::string& zone);
  // Reads out a briefing's speech by its name on the CD ("" stops it).
  void setSpeech(std::function<void(const std::string&)> speak) { speak_ = std::move(speak); }
  // Our own data (data/).
  void setOwnData(const std::string& dir) { ownData_ = dir; }
  // The Ukrainian campaign's picture: a binary PPM's path (made when first asked), "" none.
  void setUkrainePicture(std::function<std::string()> path) { ukrainePic_ = std::move(path); }
  // Reads a file of the original's CD (the GoTo menu's Animations); none: there is no CD.
  void setCd(std::function<bool(const std::string&, std::vector<uint8_t>&)> read) { cdRead_ = std::move(read); }
  // Says a voice of the original's sounds (the recognition library's unit names).
  void setVoice(std::function<void(int)> say) { say_ = std::move(say); }

private:
  // Something to press: a stone button of the picture (with its pressed brush), a red one, a
  // line of the GoTo menu.
  struct Item {
    int id;
    glm::vec2 lo, hi;
    int brush = -1;  // index into brushes_
    std::string label;
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
    bool available = true;
  };
  struct Entry {
    std::string name;
    gamedata::Mission mission;
  };
  enum class List { Training, Single };

  std::vector<Item> items() const;
  std::vector<Item> stoneButtons() const;
  FrontResult activate(int id);
  void back();
  void notAvailable();
  void openList(List l, const std::string& zone);
  void moveFocus(int step);
  int itemAt(glm::vec2 p) const;

  void panel(glm::vec2 lo, glm::vec2 hi);
  void redButton(const Item& it, bool lit = false);
  void drawMissions();
  void drawLog();
  void drawNetwork();
  void drawRecognition(int w, int h);
  void drawPrefs();
  void drawTopTen();
  void drawCampaign();
  // The campaign's page on: its next page of text, or the mission.
  FrontResult campaignNext();
  void wrapCampaign();
  // The preference `i` to its next (or previous) value, saved.
  void stepPref(int i, int dir);
  std::string prefValue(int i) const;
  // Into the configuration and hind.ini at once (what the file has, not this run's options).
  void savePrefs(const std::function<void(platform::Config&)>& change);
  // The joystick's panel: the main device the next (or previous) one or off; learning the axes
  // (each control moved when asked), a step of it each frame.
  void stepJoystick(int dir);
  void startLearning();
  void learn();
  void drawJoystickGlass();
  // The network page's rows: a pilot's game each (the host's first, his wingmen under him).
  std::vector<const net::Advert*> netRows() const;
  void saveLog();
  bool loadPicture(const std::string& name, render::Texture& t, int transparent);

  render::Canvas canvas_;
  std::string dir_;
  render::Texture main_, flight_, world_, briefing_, exit_, quickOptions_;
  render::Texture ukraine_;  // our UKRAINE button on the world page (pressed: in brushes_)
  std::vector<render::Texture> brushes_;

  Page page_ = Page::Main, exitFrom_ = Page::Main;
  Choice choice_;
  bool infiniteWeapons_ = false, noCrash_ = false, wsoAllies_ = false, gunnerHints_ = true;
  bool ukraineInterceptors_ = false;
  // The mission list: which, for which war, the missions, the one chosen, the first line shown,
  // how far the briefing is scrolled.
  List list_ = List::Training;
  bool worldForCampaign_ = false;  // the war chosen goes to its campaign, not to a list
  int quickWorld_ = 0;  // the war chosen for Quickstart: 1 Arcade, 2 Invincible (0: not)
  // The Top Ten: the table, its file, its picture, the line being entered (-1: none) and its score.
  sim::TopTen* topTen_ = nullptr;
  std::string topTenPath_;
  bool topCrashed_ = false;  // the score being entered: its flight ended in a crash
  render::Texture topTenBg_;
  int topEntry_ = -1, topScore_ = 0;
  Page topFrom_ = Page::Quick;
  // The campaign's page: the war, its state, its pages of text (each wrapped into screens of
  // lines), where it is, its picture.
  std::string campZone_;
  gamedata::CampaignState camp_;
  std::vector<gamedata::CampaignPage> campPages_;
  std::vector<std::vector<std::vector<std::string>>> campScreens_;  // each page's screens of lines
  int campPage_ = 0, campScreen_ = 0;
  render::Texture campaignBg_;
  std::function<void(const std::string&)> speak_;
  // An animation of the CD playing (Page::Animation) over the page it was started from, which it
  // goes back to at its end (or a key, a click).
  std::function<bool(const std::string&, std::vector<uint8_t>&)> cdRead_;
  gamedata::Anim anim_;
  render::Texture animTex_;
  std::vector<uint8_t> animRgb_;
  float animTime_ = 0.0f;
  Page animFrom_ = Page::Main;
  void playAnimation();
  bool animationFrame();
  std::string ownData_;
  std::function<std::string()> ukrainePic_;
  // The Ukrainian campaign's picture: ours (setUkrainePicture) in Afghanistan's frame.
  void ukrainePicture(render::Texture& t);
  std::string zone_ = "afghan";
  std::vector<Entry> entries_;
  int selected_ = -1, top_ = 0, textLine_ = 0, textLines_ = 0;

  glm::vec2 mouseScale_{1.0f}, mouse_{-1.0f};
  int pressed_ = -1;      // held down with the mouse
  int pending_ = -1;      // pressed from the keyboard, acts when the time is up
  float pendingTime_ = 0.0f;
  int focus_ = -1;        // the stone button the arrows are on
  bool keyboard_ = false; // the arrows were used: the focus shows
  bool goTo_ = false;     // the GoTo menu is open
  std::string note_;
  float noteTime_ = 0.0f;
  // The Pilot's Log: the log, its file, the medals' pictures (Red Star, Lenin, Gold Star), a
  // name being typed (for a new pilot or a new name), the awards still to show.
  sim::PilotLog* log_ = nullptr;
  std::string logPath_;
  render::Texture log_bg_, medals_[3], award_;
  bool typing_ = false, renaming_ = false;
  std::string typed_;
  std::vector<std::string> awards_;
  Page afterAwards_ = Page::Main;
  // The network game: the choices, the games heard, the row chosen, the field being typed in
  // (0 none, 1 the name, 2 the game's name, 3 an address), the host's state.
  render::Texture network_, server_;
  NetChoice net_;
  std::vector<net::Advert> games_;
  int netRow_ = -1, netField_ = 0;
  bool hosting_ = false;
  std::string hostStatus_;
  // The recognition library: its picture, which list (aircraft, vehicles, ships) and which unit
  // in it, the page it was opened from, the models (the original's data, opened when needed).
  render::Texture recognition_;
  int recList_ = 0, recItem_ = 0;
  Page recFrom_ = Page::Main;
  gamedata::OriginalData recData_;
  bool recDataTried_ = false;
  render::ModelViewer viewer_;
  std::function<void(int)> say_;
  // The preferences: the background, the configuration, the setting chosen (keyboard, hover),
  // the page it was opened from (the focus after the bars: the joystick's panel), the devices shown on the
  // panel, the axes being learnt (the step, -1: not; the map so far, the axes it has taken).
  render::Texture prefs_;
  platform::Config* cfg_ = nullptr;
  std::string cfgPath_;
  std::function<void(const std::string&, const std::string&)> changed_;
  int prefFocus_ = 0;
  Page prefsFrom_ = Page::Main;
  platform::joy::Devices joys_;
  platform::joy::Learner learner_;
  int learnStep_ = -1;
  platform::joy::Map learnMap_;
  std::vector<std::pair<int, int>> learnTaken_;
  void sayUnit();
};

}  // namespace menu
