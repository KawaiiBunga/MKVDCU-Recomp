#include "port_config.h"

#include <charconv>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <rex/cvar.h>

namespace {
// Written by the launcher for every session; persisting it only goes stale.
constexpr const char* kSessionKeys[] = {"log_file"};

std::optional<toml::table> ParseToml(std::string_view text, std::string* error = nullptr) {
#if TOML_EXCEPTIONS
  try {
    return toml::parse(text);
  } catch (const toml::parse_error& e) {
    if (error) *error = std::string(e.description());
    return std::nullopt;
  }
#else
  toml::parse_result result = toml::parse(text);
  if (!result) {
    if (error) *error = std::string(result.error().description());
    return std::nullopt;
  }
  return std::move(result).table();
#endif
}

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (file.bad()) return std::nullopt;
  return text;
}

std::string TomlString(std::string_view value) {
  std::string result = "\"";
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (ch < 0x20 || ch == 0x7f) {
          constexpr char hex[] = "0123456789ABCDEF";
          result += "\\u00";
          result += hex[ch >> 4];
          result += hex[ch & 0xf];
        } else {
          result += static_cast<char>(ch);
        }
    }
  }
  result += '"';
  return result;
}

// Replaces the file only after the new content is fully on disk, then reads it
// back so a short write or a redirected path cannot report success.
void WriteFileChecked(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) throw std::runtime_error("could not create " + path.parent_path().string());

  std::filesystem::path temp = path;
  temp += L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("could not open " + temp.string() + " for writing");
    file << content;
    file.flush();
    if (!file) throw std::runtime_error("could not write " + temp.string());
  }
#if defined(_WIN32)
  if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    std::filesystem::remove(temp, ec);
    throw std::runtime_error("could not replace " + path.string() + " (Windows error " +
                             std::to_string(error) + ")");
  }
#else
  std::filesystem::rename(temp, path, ec);
  if (ec) throw std::runtime_error("could not replace " + path.string() + ": " + ec.message());
#endif

  const auto written = ReadFile(path);
  if (!written || *written != content) {
    throw std::runtime_error("read-back of " + path.string() + " does not match what was written");
  }
}

// Mirrors how rex::cvar::LoadConfig turns a TOML value into a cvar string.
std::optional<std::string> CvarString(const toml::node& node) {
  if (const auto* v = node.as_boolean()) return v->get() ? "true" : "false";
  if (const auto* v = node.as_integer()) return std::to_string(v->get());
  if (const auto* v = node.as_floating_point()) return std::to_string(v->get());
  if (const auto* v = node.as_string()) return v->get();
  return std::nullopt;
}

template <typename T>
bool ParseNumber(std::string_view text, T& out) {
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  return ec == std::errc() && ptr == text.data() + text.size();
}

void AssignTyped(toml::table& table, const std::string& key, const std::string& value) {
  const auto* flag = rex::cvar::GetFlagInfo(key);
  int64_t guess = 0;
  // Plugin cvars are unregistered until the plugin loads; infer from the text.
  const auto type = flag                                 ? flag->type
                    : value == "true" || value == "false" ? rex::cvar::FlagType::Boolean
                    : ParseNumber(value, guess)           ? rex::cvar::FlagType::Int64
                                                          : rex::cvar::FlagType::String;
  switch (type) {
    case rex::cvar::FlagType::Boolean:
      table.insert_or_assign(key, value == "true");
      return;
    case rex::cvar::FlagType::Int32:
    case rex::cvar::FlagType::Int64:
    case rex::cvar::FlagType::Uint32:
    case rex::cvar::FlagType::Uint64: {
      int64_t number = 0;
      if (ParseNumber(value, number)) {
        table.insert_or_assign(key, number);
      } else if (type == rex::cvar::FlagType::Uint64) {
        table.insert_or_assign(key, value);  // above TOML's int64 range
      } else {
        throw std::runtime_error(key + " has a non-numeric value '" + value + "'");
      }
      return;
    }
    case rex::cvar::FlagType::Double: {
      double number = 0;
      if (!rex::cvar::ParseDouble(value, number)) {
        throw std::runtime_error(key + " has a non-numeric value '" + value + "'");
      }
      table.insert_or_assign(key, number);
      return;
    }
    default:
      table.insert_or_assign(key, value);
  }
}
}  // namespace

std::optional<toml::table> ReadPortConfig(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return std::nullopt;
  const auto text = ReadFile(path);
  return text ? ParseToml(*text) : std::nullopt;
}

void WritePortConfig(const std::filesystem::path& path,
                     const std::vector<std::pair<std::string, std::string>>& values) {
  std::map<std::string, std::string> entries;
  for (const auto& name : rex::cvar::ListFlags()) {
    if (rex::cvar::GetFlagSource(name) != rex::cvar::Source::kRuntime) continue;
    const auto* flag = rex::cvar::GetFlagInfo(name);
    if (!flag || flag->type == rex::cvar::FlagType::Command) continue;
    entries[name] = rex::cvar::GetFlagByName(name);
  }
  for (const auto& [name, value] : values) entries[name] = value;

  toml::table table;
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    const auto text = ReadFile(path);
    if (!text) throw std::runtime_error("could not read " + path.string());
    if (auto existing = ParseToml(*text)) {
      table = std::move(*existing);
    } else {
      // Unreadable anyway; keep a copy rather than silently dropping it.
      std::filesystem::path backup = path;
      backup += L".invalid";
      std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
    }
  }
  for (const auto& [name, value] : entries) AssignTyped(table, name, value);
  for (const char* name : kSessionKeys) table.erase(name);

  std::ostringstream out;
  out << "# MKVDCU-Recomp settings, written by the F1 port menu.\n\n" << table << '\n';
  const std::string content = out.str();

  std::string parse_error;
  const auto check = ParseToml(content, &parse_error);
  if (!check) throw std::runtime_error("generated settings are not valid TOML: " + parse_error);
  for (const auto& [name, value] : entries) {
    const auto* node = check->get(name);
    const auto round_trip = node ? CvarString(*node) : std::nullopt;
    if (!round_trip || *round_trip != value) {
      throw std::runtime_error(name + " would not load back as '" + value + "'");
    }
  }

  WriteFileChecked(path, content);
}

std::string SavePortCvars(const std::filesystem::path& path, const std::vector<std::string>& names) {
  std::vector<std::pair<std::string, std::string>> values;
  for (const auto& name : names) values.emplace_back(name, rex::cvar::GetFlagByName(name));
  try {
    WritePortConfig(path, values);
    return {};
  } catch (const std::exception& e) {
    return e.what();
  }
}

void RepairPortConfig(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return;
  const auto text = ReadFile(path);
  if (!text || ParseToml(*text)) return;

  // The SDK writes `key = "value"` with the value copied verbatim, so every
  // quoted value is raw text that still needs escaping.
  std::string repaired;
  std::istringstream lines(*text);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    const size_t open = eq == std::string::npos ? eq : line.find_first_not_of(" \t", eq + 1);
    if (open != std::string::npos && line[open] == '"' && line.size() > open + 1 &&
        line.back() == '"') {
      const std::string_view raw(line.data() + open + 1, line.size() - open - 2);
      line = line.substr(0, open) + TomlString(raw);
    }
    repaired += line;
    repaired += '\n';
  }

  std::filesystem::path backup = path;
  backup += L".invalid";
  std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
  try {
    if (ParseToml(repaired)) {
      WriteFileChecked(path, repaired);
      return;
    }
  } catch (const std::exception&) {
  }
  // Could not repair it: move it aside so the game starts from defaults
  // instead of failing to parse the same file on every launch.
  if (!ec) std::filesystem::remove(path, ec);
}
