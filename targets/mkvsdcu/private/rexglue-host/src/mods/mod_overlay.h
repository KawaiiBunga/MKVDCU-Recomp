#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rex::filesystem {
class VirtualFileSystem;
}

// File-replacement mods. Each mod folder mirrors the game folder; a file in a
// mod wins over the game's own file (and over mods listed after it). The
// launcher passes enabled mods as --port_mods="<dir>|<dir>", highest first.
namespace mod_overlay {

// Parses the port_mods cvar; missing folders are skipped.
std::vector<std::filesystem::path> EnabledMods();

// Re-points game: and d: at an overlay of the mods over the game folder.
// Call after the runtime mounts the game and before the title starts.
// Returns how many mods were mounted.
size_t Install(rex::filesystem::VirtualFileSystem* vfs,
               const std::vector<std::filesystem::path>& mods);

}  // namespace mod_overlay
