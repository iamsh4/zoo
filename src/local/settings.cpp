#include <cassert>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <json/json.h>

#include "local/settings.h"

namespace zoo::local {

std::shared_ptr<Settings>
safe_load_settings(std::string_view settings_root_dir, std::string_view settings_filename)
{
  // Ensure the settings folder exists
  if (!std::filesystem::is_directory(settings_root_dir)) {
    printf("Recursively creating settings folder '%s'\n", settings_root_dir.data());
    if (!std::filesystem::create_directories(settings_root_dir)) {
      fprintf(stderr, "Failed to create settings folder!\n");
      return nullptr;
    }
  }

  return std::make_shared<Settings>(settings_root_dir, settings_filename);
}

Settings::Settings() {}

Settings::Settings(std::string_view settings_root_dir, std::string_view settings_filename)
  : m_settings_root_dir(settings_root_dir),
    m_settings_filename(settings_filename)
{
  deserialize();
}

Settings::~Settings()
{
  if (!m_settings_filename.empty()) {
    serialize();
  }
}

void
Settings::set(std::string_view key, std::string_view value)
{
  assert(!strchr(key.data(), ' ') && "settings keys may not contain a space");
  m_settings[std::string(key)] = value;
}

std::string_view
Settings::get_or_default(std::string_view query, std::string_view default_value) const
{
  assert(!strchr(query.data(), ' ') && "settings keys may not contain a space");
  for (const auto &[key, val] : m_settings) {
    if (key == query) {
      return val;
    }
  }

  // Return default value
  return default_value;
}

void
Settings::erase(std::string_view key)
{
  m_settings.erase(std::string(key));
}

void
Settings::clear()
{
  m_settings = {};
}

bool
Settings::has(std::string_view key) const
{
  // TODO: This is terrible, but the settings store needs some sense of an owned
  // string. It works for now.
  return m_settings.find(std::string(key)) != m_settings.end();
}

const std::string &
Settings::settings_root_dir() const
{
  return m_settings_root_dir;
}

const std::string &
Settings::settings_filename() const
{
  return m_settings_filename;
}

const std::unordered_map<std::string, std::string> &
Settings::data() const
{
  return m_settings;
}

void
Settings::serialize()
{
  const std::filesystem::path settings_path = std::filesystem::path(m_settings_root_dir) /
                                              std::filesystem::path(m_settings_filename);

  Json::Value root;

  Json::Value &globals = root["global"];
  for (const auto &[key, value] : m_settings) {
    globals[key] = value;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"]  = "  ";

  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  std::ofstream out(settings_path, std::ofstream::binary);
  writer->write(root, &out);
  printf("Wrote file to %s\n", settings_path.c_str());
}

void
Settings::deserialize()
{
  const std::filesystem::path settings_path = std::filesystem::path(m_settings_root_dir) /
                                              std::filesystem::path(m_settings_filename);

  ensure_file_exists();

  try {
    std::ifstream in(settings_path);
    Json::Value root;
    in >> root;

    m_settings.clear();
    Json::Value &globals = root["global"];
    for (Json::ValueIterator it = globals.begin(); it != globals.end(); it++) {
      const std::string key   = it.key().asString();
      const std::string value = it->asString();
      m_settings[key]         = value;
    }

  } catch (std::exception &) {
    printf("Failed to load settings, skipping\n");
  }
}

void
Settings::ensure_file_exists()
{
  const std::filesystem::path settings_path = std::filesystem::path(m_settings_root_dir) /
                                              std::filesystem::path(m_settings_filename);

  FILE *fp = fopen(settings_path.c_str(), "r");
  if (!fp) {
    fp = fopen(settings_path.c_str(), "w");
  }
  fclose(fp);
}

} // namespace zoo::local
