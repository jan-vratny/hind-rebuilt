#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The Pilot's Log, as the original's (manual pp. 13-14): up to ten pilots, the first of them
// Comrade Immortal, who cannot come to harm. Each logs his flying time, his missions and how
// they went, the campaigns won; he rises in rank by the missions flown and is decorated for
// campaigns and single missions won. A crash may put him out of action for some real hours
// (injured, missing) or for good (killed), and then nothing more is logged for him.
namespace sim {

enum class PilotStatus { Active, Injured, Missing, Pow, Kia, Kit };
enum class Rank { Sergeant, Lieutenant, Captain, Major, Colonel };
// Medals, as bits.
enum Medal : unsigned { kRedStar = 1, kLenin = 2, kGoldStar = 4 };

struct Pilot {
  std::string name;
  bool immortal = false;
  PilotStatus status = PilotStatus::Active;
  int64_t until = 0;  // Unix time the status ends (injured, missing, a prisoner); 0 for good
  double totalSeconds = 0.0, combatSeconds = 0.0;  // airborne
  int missions = 0, combatMissions = 0, outstanding = 0, successful = 0, failed = 0;
  int singlesWon = 0;                     // single (not campaign) missions won
  std::vector<std::string> campaignsWon;  // zone keys
  std::vector<std::string> campaignsFlown;
  std::vector<std::string> failedMissions;  // missions whose last logged flight failed (file names, the list's x)
  unsigned medals = 0;
};

// What one flight was, to be logged.
struct FlightRecord {
  double airborneSeconds = 0.0;
  bool combat = true;     // not a training mission
  bool campaign = false;  // a mission of a campaign (else a single one)
  std::string zone;       // its war: afghan, kazak, korea, ukraine (saratov for training)
  std::string mission;    // its file name (AFGHNS01)
  int result = 0;         // 0 failed, 1 success, 2 outstanding
  bool campaignWon = false;  // the campaign's last mission won
  bool crashed = false, died = false, enemyTerritory = false;
};

// What the flight brought: a promotion, medals (to be shown), the pilot's fate.
struct LogOutcome {
  bool logged = false;  // false: the pilot is out of action, nothing logged
  int promotedTo = -1;  // Rank, or -1
  unsigned newMedals = 0;
  PilotStatus status = PilotStatus::Active;
};

const char* rankName(Rank r);
const char* statusName(PilotStatus s);
// Missions flown for each rank (manual p. 14: 0, 5, 15, 30, 50).
Rank rankFor(int missions);

class PilotLog {
public:
  static constexpr int kMaxPilots = 10;
  // Real hours out of action (manual p. 13).
  static constexpr int kInjuredHours = 24, kMissingHours = 48, kPowHours = 96;

  // From the file (a new log with Comrade Immortal when there is none); false when it was damaged.
  bool load(const std::string& path);
  bool save(const std::string& path) const;
  void reset();

  std::vector<Pilot> pilots;
  int current = 0;

  Pilot& pilot() { return pilots[size_t(current)]; }
  const Pilot& pilot() const { return pilots[size_t(current)]; }
  // A new pilot (false: the log is full or the name empty).
  bool create(const std::string& name);
  // Comrade Immortal stays.
  bool remove(int index);
  // Statuses whose time is up (`now`: Unix time) go back to Active.
  void refresh(int64_t now);
  bool canLog(int64_t now) const;
  LogOutcome record(const FlightRecord& f, int64_t now);
};

}  // namespace sim
