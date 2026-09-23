#pragma once

#include <functional>
#include <string>
#include <vector>

// Every setting the port menu shows, in display order: one cvar, one control.
// The menu draws rows marked kCustom itself (window size, key bindings).
namespace settings_catalog {

enum class Apply {
  kLive,     // the game reads the cvar while running
  kRestart,  // read at startup; the menu saves it for the next launch
};

struct Choice {
  std::string value;
  std::string label;
};

// Current value of a setting as the menu shows it (the pending value for
// restart settings).
using ValueOf = std::function<std::string(const char* cvar)>;

struct Setting {
  const char* cvar;  // for kCustom, the menu's id for the row
  const char* label;
  const char* help;  // one sentence, shown in the menu's help line
  Apply apply;
  enum class Kind { kBool, kChoice, kSegmented, kInt, kPercent, kCustom } kind;
  std::vector<Choice> choices;  // kChoice / kSegmented
  int min = 0, max = 0;         // kInt / kPercent
  // Shown only when this graphics backend is in use ("d3d12", "vulkan").
  const char* backend = nullptr;
  // Shown only when this returns true.
  std::function<bool(const ValueOf&)> visible;
};

struct Group {
  const char* tab;  // DISPLAY, GRAPHICS, CONTROLS, AUDIO, PERFORMANCE, ADVANCED
  const char* title;
  std::vector<Setting> settings;
};

// Built on first use; choices that depend on the machine (GPU adapters,
// monitors, compiled-in backends and output filters) are filled in then.
const std::vector<Group>& Groups();

// Every cvar the catalog saves, split by when it applies.
std::vector<const char*> LiveCvars();
std::vector<const char*> RestartCvars();

}  // namespace settings_catalog
