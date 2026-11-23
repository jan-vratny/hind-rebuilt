#include "gamedata/campaign.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "gamedata/mission.hpp"

namespace gamedata {

namespace {

const char* const kShows[] = {"intro", "situation", "stalemate", "failure", "won"};

std::string twoDigits(int n) {
  char b[16];
  std::snprintf(b, sizeof b, "%02d", n);
  return b;
}

std::string section(const std::vector<std::pair<std::string, std::string>>& s, const std::string& name) {
  for (const auto& [k, v] : s)
    if (k == name) return v;
  return "";
}

}  // namespace

std::string campaignPrefix(const std::string& zone) {
  return zone == "kazak" ? "KAZAK" : zone == "korea" ? "KOREA" : zone == "ukraine" ? "UKRAI" : "AFGHN";
}

int campaignLevels(const std::string& gameDir, const std::string& zone) {
  int n = 0;
  for (const std::string& m : listMissions(gameDir, campaignPrefix(zone) + "C")) n = std::max(n, missionLevel(gameDir, m));
  return n;
}

CampaignState loadCampaign(const platform::Config& cfg, const std::string& zone, const std::string& gameDir) {
  CampaignState s;
  const std::string level = cfg.getString("campaign", zone + "_level", "");
  if (level.empty()) {
    // An older hind.ini: the number of the campaign's mission flown next.
    const int old = cfg.getInt("campaign", zone, 1);
    if (old > 1) {
      s.level = std::max(1, missionLevel(gameDir, campaignPrefix(zone) + "C" + twoDigits(old)));
      s.show = CampaignShow::Situation;
    }
    return s;
  }
  s.level = std::max(1, std::atoi(level.c_str()));
  s.lives = std::clamp(cfg.getInt("campaign", zone + "_lives", CampaignState::kLives), 0, CampaignState::kLives);
  std::stringstream in(cfg.getString("campaign", zone + "_failed", ""));
  for (std::string m; std::getline(in, m, ',');)
    if (!m.empty()) s.failed.push_back(m);
  s.mission = cfg.getString("campaign", zone + "_mission", "");
  const std::string show = cfg.getString("campaign", zone + "_show", "intro");
  for (int i = 0; i < 5; ++i)
    if (show == kShows[i]) s.show = CampaignShow(i);
  return s;
}

void saveCampaign(platform::Config& cfg, const std::string& zone, const CampaignState& s) {
  std::string failed;
  for (const std::string& m : s.failed) failed += (failed.empty() ? "" : ",") + m;
  cfg.set("campaign", zone + "_level", std::to_string(s.level));
  cfg.set("campaign", zone + "_lives", std::to_string(s.lives));
  cfg.set("campaign", zone + "_failed", failed);
  cfg.set("campaign", zone + "_mission", s.mission);
  cfg.set("campaign", zone + "_show", kShows[int(s.show)]);
}

std::string pickCampaignMission(const CampaignState& s, const std::string& gameDir, const std::string& zone, unsigned seed) {
  std::vector<std::string> untried, failed;
  for (const std::string& m : listMissions(gameDir, campaignPrefix(zone) + "C")) {
    if (missionLevel(gameDir, m) != s.level) continue;
    (std::find(s.failed.begin(), s.failed.end(), m) == s.failed.end() ? untried : failed).push_back(m);
  }
  const std::vector<std::string>& from = untried.empty() ? failed : untried;
  return from.empty() ? "" : from[seed % from.size()];
}

void campaignResult(CampaignState& s, bool success, int levels) {
  if (success) {
    ++s.level;
    s.failed.clear();
    s.show = s.level > levels ? CampaignShow::Won : CampaignShow::Situation;
  } else {
    if (!s.mission.empty() && std::find(s.failed.begin(), s.failed.end(), s.mission) == s.failed.end()) s.failed.push_back(s.mission);
    s.lives = std::max(0, s.lives - 1);
    s.show = s.lives > 0 ? CampaignShow::Stalemate : CampaignShow::Failure;
  }
  s.mission.clear();
}

void restartCampaign(CampaignState& s) { s = CampaignState{}; }

std::vector<CampaignPage> campaignPages(const CampaignState& s, const std::string& gameDir, const std::string& zone) {
  const std::string p = campaignPrefix(zone);
  const std::string lv = twoDigits(std::max(1, s.level));
  const auto intro = loadTextSections(gameDir, p + "L00");
  const auto level = loadTextSections(gameDir, p + "L" + lv);
  std::vector<CampaignPage> out;
  auto add = [&](const std::string& heading, const std::string& text, const std::string& speech) {
    if (!text.empty()) out.push_back({heading, text, speech});
  };
  const std::string levelHeading = "Campaign level " + std::to_string(s.level);
  switch (s.show) {
    case CampaignShow::Intro:
      add("The campaign", section(intro, "Description"), p + "L00");
      add(levelHeading, section(level, "Situation"), p + "L" + lv);
      break;
    case CampaignShow::Situation: add(levelHeading, section(level, "Situation"), p + "L" + lv); break;
    case CampaignShow::Stalemate: add(levelHeading + ": stalemate", section(level, "Stalemate"), p + "E" + lv); break;
    case CampaignShow::Failure: add("The campaign is lost", section(level, "Failure"), p + "D" + lv); break;
    case CampaignShow::Won: add("The campaign is won", section(intro, "Successful"), p + "A00"); break;
  }
  return out;
}

}  // namespace gamedata
