#pragma once
#include <map>
#include <string>
#include <vector>

#include "sim/targets.hpp"
#include "sim/mission.hpp"

// The Top Ten (manual p. 6): the ten highest scores of Quickstart Arcade. A flight scores by
// the targets the player destroys (Invincible scores nothing); a score high enough gets a blank
// line on the Top Ten screen for the pilot's name.
namespace sim {

struct TopTenEntry {
  std::string name;
  int score = 0;
  bool crashed = false;  // the flight ended in a crash (else the player ended it)
};

class TopTen {
public:
  static constexpr int kSize = 10;
  bool load(const std::string& path);
  bool save(const std::string& path) const;
  // Where `score` would go (0 the top), or -1 when it is not high enough.
  int place(int score) const;
  // Puts it in at its place, the last one dropping off; its place, or -1.
  int insert(const std::string& name, int score, bool crashed = false);

  std::vector<TopTenEntry> entries;  // best first, at most kSize
};

// Points for destroying a unit in Arcade: by what it is and what it can do. The enemy's count,
// our own (and the neutral) count against.
int arcadePoints(const Vehicle& v);
// The mission's result on top: success, outstanding.
constexpr int kArcadeSuccess = 250, kArcadeOutstanding = 500;

// What the flight in Arcade (and Invincible) comes to: the mission's targets do not count there.
// Down: failure. Still flying when it ended: success, outstanding with kArcadeOutstandingScore.
constexpr int kArcadeOutstandingScore = 3000;
MissionResult arcadeResult(bool crashed, int score);
// What the player destroyed, by unit's name.
struct ArcadeKill {
  int count = 0, points = 0;
};
// The debriefing: the score (`scored`: Invincible has none, `bonus` of it for the result), the
// time in the air, how the Hind was lost and the list of what was destroyed, most points first.
std::vector<std::string> arcadeDebrief(bool crashed, const std::string& lost, bool scored, int score, int bonus, double airborne,
                                       const std::map<std::string, ArcadeKill>& kills);

}  // namespace sim
