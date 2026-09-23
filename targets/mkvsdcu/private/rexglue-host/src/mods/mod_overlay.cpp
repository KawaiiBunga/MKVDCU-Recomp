#include "mod_overlay.h"

#include <algorithm>
#include <memory>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/string/utf8.h>

REXCVAR_DEFINE_STRING(port_mods, "", "Port/Mods",
                      "Mod folders layered over the game data, highest priority first, "
                      "separated by |");

namespace mod_overlay {
namespace {
using rex::filesystem::Entry;
using rex::filesystem::FileInfo;
using rex::filesystem::HostPathEntry;

constexpr const char* kGameMount = "\\Device\\Harddisk0\\Partition1";

// Entry keeps its children protected and has no API to replace one. A pointer
// to member taken through a derived class is an ordinary, well-formed way to
// reach it.
struct EntryAccess : Entry {
  static constexpr auto kChildren = &EntryAccess::children_;
};

std::vector<std::unique_ptr<Entry>>& Children(Entry* entry) {
  return entry->*EntryAccess::kChildren;
}

bool IsDirectory(const Entry* entry) {
  return entry->attributes() & rex::filesystem::kFileAttributeDirectory;
}

// Puts a mod file (or folder) into the game's tree as @p name under @p parent,
// replacing a game entry of the same name. Everything that looks the game's
// files up - absolute paths, opens relative to a folder, directory listings -
// then sees the mod's file. Returns the entry now in the tree.
Entry* Graft(Entry* parent, const std::filesystem::path& host_path, const std::string& name,
             FileInfo info) {
  info.name = rex::to_path(name);
  auto& children = Children(parent);
  auto entry = std::unique_ptr<Entry>(
      HostPathEntry::Create(parent->device(), parent, host_path, info));
  Entry* result = entry.get();
  const auto existing = std::find_if(children.begin(), children.end(), [&](const auto& child) {
    return rex::string::utf8_equal_case(child->name(), name);
  });
  if (existing != children.end()) {
    *existing = std::move(entry);
  } else {
    children.push_back(std::move(entry));
  }
  return result;
}

// Merges one mod folder into @p game_dir. Returns files replaced or added.
size_t Merge(Entry* game_dir, const std::filesystem::path& mod_dir) {
  size_t files = 0;
  std::error_code error;
  for (const auto& item : std::filesystem::directory_iterator(mod_dir, error)) {
    const std::string name = rex::path_to_utf8(item.path().filename());
    FileInfo info;
    if (!rex::filesystem::GetInfo(item.path(), &info)) continue;
    Entry* existing = game_dir->GetChild(name);
    // Keep the game's spelling of a name; the title may compare it.
    const std::string game_name = existing ? existing->name() : name;
    if (info.type == FileInfo::Type::kDirectory) {
      Entry* dir = existing && IsDirectory(existing) ? existing
                                                     : Graft(game_dir, item.path(), game_name, info);
      files += Merge(dir, item.path());
    } else {
      if (game_dir->parent() == nullptr && rex::string::utf8_equal_case(name, "default.xex")) {
        continue;  // the executable is recompiled; it cannot be modded as a file
      }
      Graft(game_dir, item.path(), game_name, info);
      REXLOG_DEBUG("Mod file: {} <- {}", game_name, rex::path_to_utf8(item.path()));
      ++files;
    }
  }
  return files;
}
}  // namespace

std::vector<std::filesystem::path> EnabledMods() {
  std::vector<std::filesystem::path> mods;
  const std::string list = REXCVAR_GET(port_mods);
  size_t start = 0;
  while (start <= list.size()) {
    const size_t end = std::min(list.find('|', start), list.size());
    const std::string item = list.substr(start, end - start);
    if (!item.empty()) {
      std::error_code error;
      const auto path = rex::to_path(item);
      if (std::filesystem::is_directory(path, error)) {
        mods.push_back(path);
      } else {
        REXLOG_WARN("Mod folder not found, skipped: {}", item);
      }
    }
    start = end + 1;
  }
  return mods;
}

size_t Install(rex::filesystem::VirtualFileSystem* vfs,
               const std::vector<std::filesystem::path>& mods) {
  if (!vfs || mods.empty()) return 0;
  Entry* root = vfs->ResolvePath(kGameMount);
  if (!root) {
    REXLOG_ERROR("Mods not loaded: the game folder is not mounted");
    return 0;
  }
  size_t loaded = 0;
  // Lowest priority first, so higher mods overwrite what lower ones placed.
  for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
    const size_t files = Merge(root, *it);
    REXLOG_INFO("Mod loaded: {} ({} files)", rex::path_to_utf8(*it), files);
    ++loaded;
  }
  return loaded;
}

}  // namespace mod_overlay
