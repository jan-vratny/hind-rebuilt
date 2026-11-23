#pragma once
#include <string>
#include <vector>

#include "platform/config.hpp"

// A war's campaign as the original's: ten levels (ours in Ukraine eight) of two or three
// missions each. A level is flown with one of its missions, picked at random among those not
// yet tried (then among those failed); a success goes up a level, a failure costs a life and
// the level is tried again with another mission, the last life lost ends the campaign. Between
// the missions the campaign's page (C_AFGHAN.LBM ...) tells how it stands, read out from the CD:
// the campaign's description at its start (AFGHNL00 ":Description", speech AFGHNL00), each
// level's situation (AFGHNL01 ":Situation", AFGHNL01), a stalemate after a failure (":Stalemate",
// AFGHNE01), the level's failure ending it (":Failure", AFGHND01), the campaign won (AFGHNL00
// ":Successful", AFGHNA00).
namespace gamedata {

enum class CampaignShow { Intro, Situation, Stalemate, Failure, Won };

struct CampaignState {
  static constexpr int kLives = 3;
  int level = 1;
  int lives = kLives;
  std::vector<std::string> failed;  // the level's missions failed
  std::string mission;              // the one picked to fly
  CampaignShow show = CampaignShow::Intro;
};

// The war's prefix of the missions' names: "AFGHN", "KAZAK", "KOREA", "UKRAI".
std::string campaignPrefix(const std::string& zone);
// Its levels: the highest of its missions'.
int campaignLevels(const std::string& gameDir, const std::string& zone);

// [campaign] <zone>_level, _lives, _failed, _mission, _show (an older hind.ini's <zone> = the
// mission number: its level).
CampaignState loadCampaign(const platform::Config& cfg, const std::string& zone, const std::string& gameDir);
void saveCampaign(platform::Config& cfg, const std::string& zone, const CampaignState& s);

// A mission for the level (untried first, then failed, at random by `seed`), "" when it has none.
std::string pickCampaignMission(const CampaignState& s, const std::string& gameDir, const std::string& zone, unsigned seed);
// The mission flown: up a level or a life less, and what the page tells next.
void campaignResult(CampaignState& s, bool success, int levels);
// The campaign over (won or lost and told): from the start.
void restartCampaign(CampaignState& s);

// What the page tells (texts: a section of a level's file) and the speech that reads it.
struct CampaignPage {
  std::string heading, text, speech;
};
std::vector<CampaignPage> campaignPages(const CampaignState& s, const std::string& gameDir, const std::string& zone);

}  // namespace gamedata
