#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "shared/span.h"

namespace zoo::local {

// Program settings
// - Frontend settings (gamedirs)
// - Controls + Hotkeys
// - Emulation Flags
// Emulator persistence
// - Save Files
// - GameDir location and index file

class Settings {
public:
  /** Load and save settings from/to a file */
  Settings(std::string_view settings_root_dir, std::string_view settings_filename);

  /** Settings temporarily stored in memory */
  Settings();

  /** Serializes settings to file if one was provided at construction */
  ~Settings();

  /** If present, returns a setting and true, else returns the default and false. */
  std::string_view get_or_default(std::string_view key, std::string_view default_value) const;

  /** Sets a setting */
  void set(std::string_view key, std::string_view value);

  bool has(std::string_view key) const;

  void erase(std::string_view key);
  void clear();

  const std::unordered_map<std::string, std::string> &data() const;

  const std::string &settings_root_dir() const;
  const std::string &settings_filename() const;

private:
  const std::string m_settings_root_dir;
  const std::string m_settings_filename;

  std::unordered_map<std::string, std::string> m_settings;

  void serialize();
  void deserialize();
  void ensure_file_exists();
};

std::shared_ptr<Settings> safe_load_settings(std::string_view settings_root_dir,
                                             std::string_view settings_filename);

}
