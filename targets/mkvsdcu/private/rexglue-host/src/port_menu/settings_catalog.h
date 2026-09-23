#pragma once

#include <string>
#include <vector>

// Declarative list of the plain settings the port menu shows: one cvar, one
// control. Settings needing custom widgets (window size, render scale,
// controller preview, key bindings) are drawn by the menu itself.
namespace settings_catalog {

enum class Apply {
  kLive,     // the game reads the cvar while running
  kRestart,  // read at startup; the menu saves it for the next launch
};

struct Choice {
  std::string value;
  std::string label;
};

struct Setting {
  const char* cvar;
  const char* label;
  const char* hint;
  Apply apply;
  enum class Kind { kBool, kChoice, kInt, kPercent } kind;
  std::vector<Choice> choices;  // kChoice
  int min = 0, max = 0;         // kInt / kPercent
  // Shown only when this graphics backend is in use ("d3d12", "vulkan").
  const char* backend = nullptr;
};

struct Group {
  const char* tab;  // "GRAPHICS", "PERFORMANCE", "AUDIO", "SYSTEM"
  const char* title;
  const char* detail;
  std::vector<Setting> settings;
};

// Built on first use; choices that depend on the machine (GPU adapters,
// compiled-in backends and output filters) are filled in then.
const std::vector<Group>& Groups();

// Every cvar the catalog saves, split by when it applies.
std::vector<const char*> LiveCvars();
std::vector<const char*> RestartCvars();

}  // namespace settings_catalog
