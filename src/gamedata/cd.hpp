#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The original's CD: what it has and the installation does not (the briefings' speech in
// SPEECH/*.RAW). Read from a directory (the CD mounted, or its files copied) or straight from
// the CD's ISO image.
namespace gamedata {

class Cd {
public:
  // `configured`: [data] cd, a directory or an .iso; empty: looked for by the installation
  // (its SPEECH directory, its DATA directory as GOG's package has the CD, an .iso with "hind"
  // in its name beside it or a few levels up, also in a CDs directory there).
  bool open(const std::string& configured, const std::string& originalDir);
  bool isOpen() const { return !dir_.empty() || !iso_.empty(); }
  const std::string& where() const { return iso_.empty() ? dir_ : iso_; }
  // A file by its path on the CD ("SPEECH/AFGHNL00.RAW", any case); false when it is not there.
  bool read(const std::string& path, std::vector<uint8_t>& out) const;
  // What a directory on the CD holds ("" the root, "3DDATA", any case): names in upper case and
  // whether each is a directory; false when there is no such directory.
  struct Item {
    std::string name;
    bool dir = false;
  };
  bool listDir(const std::string& path, std::vector<Item>& out) const;

private:
  bool openIso(const std::string& path);
  std::string dir_, iso_;
  struct Entry {
    std::string name;  // upper case, without ";1"
    uint32_t lba = 0, size = 0;
    bool dir = false;
  };
  // The ISO's directories read so far, by path ("" the root).
  bool list(uint32_t lba, uint32_t size, std::vector<Entry>& out) const;
  Entry root_;
};

// The speech of a mission's briefing or its debriefing on the CD (or ours in data/speech):
// "AFGHNS03" briefing AFGHNS03, outstanding AFGHNO03, success AFGHNP03, failure AFGHNF03.
enum class SpeechPart { Briefing, Outstanding, Success, Failure };
std::string speechName(const std::string& mission, SpeechPart part);
// 8-bit unsigned mono at 22050 Hz.
constexpr int kSpeechRate = 22050;

}  // namespace gamedata
