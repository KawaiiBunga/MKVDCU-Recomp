#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

// ReXGlue's own serializer writes Windows paths without escaping, which leaves
// a config the next launch cannot parse. Call before the SDK loads the config
// to rewrite such a file as valid TOML. The original is kept as *.invalid.
void RepairPortConfig(const std::filesystem::path& path);

// Parsed config file, or nullopt when it is missing or not valid TOML.
std::optional<toml::table> ReadPortConfig(const std::filesystem::path& path);

// Writes `values` over the existing config, keeping every other key the file
// already had plus anything changed this session from the console. Throws
// with a user-facing reason when the file was not saved.
void WritePortConfig(const std::filesystem::path& path,
                     const std::vector<std::pair<std::string, std::string>>& values);

// Saves the current value of each named cvar. Returns an error, or empty.
std::string SavePortCvars(const std::filesystem::path& path, const std::vector<std::string>& names);
