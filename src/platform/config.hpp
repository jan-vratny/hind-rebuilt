#pragma once
#include <map>
#include <string>

namespace platform {

// Minimal INI file: [section] key=value, ';' or '#' comments.
class Config {
public:
  bool load(const std::string& path);
  bool save(const std::string& path) const;

  std::string getString(const std::string& section, const std::string& key, const std::string& def) const;
  int getInt(const std::string& section, const std::string& key, int def) const;
  float getFloat(const std::string& section, const std::string& key, float def) const;
  bool getBool(const std::string& section, const std::string& key, bool def) const;
  void set(const std::string& section, const std::string& key, const std::string& value);

  // Fills in every missing key with its default, so the saved file documents all options.
  void applyDefaults();

private:
  std::map<std::string, std::map<std::string, std::string>> data_;
};

// ~/.config/hind/hind.ini (or ./hind.ini if it exists).
std::string defaultConfigPath();

// The user's Pictures folder and Hind in it (XDG_PICTURES_DIR on Linux, so a localised one too).
std::string defaultScreenshotDir();

}  // namespace platform
