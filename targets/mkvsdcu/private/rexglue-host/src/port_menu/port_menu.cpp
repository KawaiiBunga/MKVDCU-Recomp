#include "port_menu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <rex/cvar.h>

#include "host_tweaks.h"
#include "input/controller_filter.h"
#include "port_menu/port_config.h"
#include "telemetry/frame_telemetry.h"
#include "telemetry/guest_profiler.h"
#include "telemetry/perf_overlay.h"
#include "telemetry/system_telemetry.h"

REXCVAR_DEFINE_INT32(port_debug_menu_cycle, 0, "Port/Debug",
                     "Developer aid: open the menu at startup and switch tabs every N seconds")
    .range(0, 60);

namespace {
using settings_catalog::Apply;
using settings_catalog::Setting;
using Kind = Setting::Kind;

constexpr const char* kTabs[] = {"DISPLAY", "GRAPHICS",   "CONTROLS", "AUDIO",
                                 "PERFORMANCE", "ADVANCED", "ABOUT"};
constexpr int kTabCount = int(std::size(kTabs));

constexpr ImU32 kRed = IM_COL32(142, 59, 55, 255);
constexpr ImU32 kBlue = IM_COL32(55, 82, 106, 255);
constexpr ImVec4 kBrass(0.73f, 0.64f, 0.49f, 1.0f);
constexpr ImVec4 kWarning(0.85f, 0.50f, 0.38f, 1.0f);
constexpr ImVec4 kError(0.92f, 0.38f, 0.34f, 1.0f);
constexpr ImVec4 kOk(0.55f, 0.72f, 0.56f, 1.0f);
constexpr ImVec4 kSelected(0.58f, 0.40f, 0.27f, 1.0f);
constexpr ImVec4 kDim(0.60f, 0.60f, 0.58f, 1.0f);

constexpr float kControlWidth = 320.0f;

// Saved alongside the catalog but edited by custom rows.
constexpr const char* kExtraLiveKeys[] = {"window_width", "window_height", "keybind_a",
                                          "keybind_b",    "keybind_x",     "keybind_y"};
constexpr const char* kKeyFlags[] = {"keybind_a", "keybind_b", "keybind_x", "keybind_y"};
constexpr const char* kKeyLabels[] = {"A key", "B key", "X key", "Y key"};

constexpr std::pair<uint32_t, uint32_t> kWindowSizes[] = {
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};

std::vector<const char*> LiveKeys() {
  std::vector<const char*> keys = settings_catalog::LiveCvars();
  keys.insert(keys.end(), std::begin(kExtraLiveKeys), std::end(kExtraLiveKeys));
  return keys;
}

std::string Cvar(const char* name) {
  return rex::cvar::GetFlagByName(name);
}

// Doubles print as "60.000000" or "60" depending on where they came from.
bool SameValue(const char* name, const std::string& a, const std::string& b) {
  if (a == b) return true;
  const auto* flag = rex::cvar::GetFlagInfo(name);
  if (!flag || flag->type != rex::cvar::FlagType::Double) return false;
  double x = 0, y = 0;
  return rex::cvar::ParseDouble(a, x) && rex::cvar::ParseDouble(b, y) && std::abs(x - y) < 1e-6;
}

// Same conversion rex::cvar::LoadConfig applies to a TOML value.
std::optional<std::string> NodeString(const toml::node& node) {
  if (const auto* v = node.as_boolean()) return v->get() ? "true" : "false";
  if (const auto* v = node.as_integer()) return std::to_string(v->get());
  if (const auto* v = node.as_floating_point()) return std::to_string(v->get());
  if (const auto* v = node.as_string()) return v->get();
  return std::nullopt;
}

std::string ChoiceLabel(const Setting& setting, const std::string& value) {
  for (const auto& choice : setting.choices) {
    if (SameValue(setting.cvar, choice.value, value)) return choice.label;
  }
  return value.empty() ? "Automatic" : value;
}

void GroupTitle(const char* title) {
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextColored(kBrass, "%s", title);
  ImGui::Separator();
}

bool Segmented(const char* id, int& index, const std::vector<std::string>& labels) {
  bool changed = false;
  const int count = int(labels.size());
  ImGui::PushID(id);
  const float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
  const float width = (ImGui::GetContentRegionAvail().x - spacing * (count - 1)) / count;
  for (int i = 0; i < count; ++i) {
    if (i) ImGui::SameLine(0, spacing);
    const bool selected = index == i;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, kSelected);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSelected);
    }
    if (ImGui::Button(labels[i].c_str(), ImVec2(width, 0)) && !selected) {
      index = i;
      changed = true;
    }
    if (selected) ImGui::PopStyleColor(2);
  }
  ImGui::PopID();
  return changed;
}

// Stick position with its deadzone ring, as the game will see it.
void DrawStick(int16_t x, int16_t y, int deadzone_percent) {
  const float radius = 22.0f;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 center(origin.x + radius + 2, origin.y + radius + 2);
  ImGui::Dummy(ImVec2(radius * 2 + 4, radius * 2 + 4));
  auto* draw = ImGui::GetWindowDrawList();
  draw->AddCircleFilled(center, radius, IM_COL32(28, 30, 33, 255));
  draw->AddCircle(center, radius, IM_COL32(110, 108, 104, 255));
  if (deadzone_percent > 0) {
    draw->AddCircleFilled(center, radius * deadzone_percent / 100.0f, IM_COL32(120, 70, 55, 150));
  }
  const ImVec2 dot(center.x + radius * x / 32767.0f, center.y - radius * y / 32767.0f);
  draw->AddCircleFilled(dot, 4.0f, IM_COL32(215, 190, 140, 255));
}

std::string PressedButtons(const port_input::PadSnapshot& pad) {
  static constexpr std::pair<uint16_t, const char*> kNames[] = {
      {0x1000, "A"},     {0x2000, "B"},     {0x4000, "X"},    {0x8000, "Y"},
      {0x0100, "LB"},    {0x0200, "RB"},    {0x0010, "Start"}, {0x0020, "Back"},
      {0x0001, "Up"},    {0x0002, "Down"},  {0x0004, "Left"}, {0x0008, "Right"},
      {0x0040, "LS"},    {0x0080, "RS"},    {0x0400, "Guide"}};
  std::string text;
  for (const auto& [bit, name] : kNames) {
    if (pad.buttons & bit) text += std::string(text.empty() ? "" : " ") + name;
  }
  if (pad.left_trigger > 30) text += std::string(text.empty() ? "" : " ") + "LT";
  if (pad.right_trigger > 30) text += std::string(text.empty() ? "" : " ") + "RT";
  return text;
}

struct StyleColor {
  ImGuiCol index;
  ImVec4 color;
};

constexpr StyleColor kPalette[] = {
    {ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.12f, 0.98f)},
    {ImGuiCol_Border, ImVec4(0.40f, 0.39f, 0.37f, 1.0f)},
    {ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)},
    {ImGuiCol_PopupBg, ImVec4(0.14f, 0.15f, 0.16f, 1.0f)},
    {ImGuiCol_FrameBg, ImVec4(0.16f, 0.17f, 0.18f, 1.0f)},
    {ImGuiCol_FrameBgHovered, ImVec4(0.24f, 0.25f, 0.26f, 1.0f)},
    {ImGuiCol_FrameBgActive, ImVec4(0.31f, 0.25f, 0.24f, 1.0f)},
    {ImGuiCol_Button, ImVec4(0.20f, 0.21f, 0.22f, 1.0f)},
    {ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.29f, 0.26f, 1.0f)},
    {ImGuiCol_ButtonActive, ImVec4(0.58f, 0.40f, 0.27f, 1.0f)},
    {ImGuiCol_Header, ImVec4(0.26f, 0.32f, 0.37f, 1.0f)},
    {ImGuiCol_HeaderHovered, ImVec4(0.32f, 0.33f, 0.34f, 1.0f)},
    {ImGuiCol_HeaderActive, ImVec4(0.45f, 0.25f, 0.23f, 1.0f)},
    {ImGuiCol_Tab, ImVec4(0.13f, 0.14f, 0.15f, 1.0f)},
    {ImGuiCol_TabHovered, ImVec4(0.26f, 0.27f, 0.28f, 1.0f)},
    {ImGuiCol_TabSelected, ImVec4(0.21f, 0.27f, 0.32f, 1.0f)},
    {ImGuiCol_TabSelectedOverline, ImVec4(0.73f, 0.64f, 0.49f, 1.0f)},
    {ImGuiCol_CheckMark, ImVec4(0.73f, 0.64f, 0.49f, 1.0f)},
    {ImGuiCol_SliderGrab, ImVec4(0.67f, 0.47f, 0.32f, 1.0f)},
    {ImGuiCol_SliderGrabActive, ImVec4(0.78f, 0.56f, 0.38f, 1.0f)},
    {ImGuiCol_Separator, ImVec4(0.30f, 0.30f, 0.29f, 1.0f)},
    {ImGuiCol_NavCursor, ImVec4(0.85f, 0.70f, 0.45f, 1.0f)},
};
}  // namespace

PortMenuDialog::PortMenuDialog(rex::ui::ImGuiDrawer* drawer, const PortHost* host,
                               std::function<void()> on_closed)
    : ImGuiDialog(drawer), host_(host), on_closed_(std::move(on_closed)) {
  // A button still held from the chord that opened the menu is not a press.
  const auto pads = port_input::ConnectedPads();
  if (!pads.empty()) last_pad_buttons_ = pads.front().buttons;
  if (host_->sampler) host_->sampler->SetWanted("menu", true);
  Load();
  for (const char* name : LiveKeys()) open_live_[name] = Cvar(name);
  open_next_ = next_;
}

PortMenuDialog::~PortMenuDialog() {
  if (host_->sampler) host_->sampler->SetWanted("menu", false);
  ImGuiIO& io = GetIO();
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
}

void PortMenuDialog::Load() {
  for (int i = 0; i < 4; ++i) {
    const std::string value = Cvar(kKeyFlags[i]);
    const size_t count = std::min(value.size(), sizeof(keys_[i]) - 1);
    std::memcpy(keys_[i], value.data(), count);
    keys_[i][count] = 0;
  }
  running_.clear();
  next_.clear();
  for (const char* name : settings_catalog::RestartCvars()) {
    running_[name] = Cvar(name);
    next_[name] = running_[name];
  }
  if (const auto config = ReadPortConfig(host_->config_path)) {
    for (const char* name : settings_catalog::RestartCvars()) {
      if (const auto* node = config->get(name)) {
        if (auto value = NodeString(*node)) next_[name] = *value;
      }
    }
  }
  dirty_ = false;
}

std::string PortMenuDialog::ValueOf(const char* name) const {
  if (const auto it = next_.find(name); it != next_.end()) return it->second;
  return Cvar(name);
}

void PortMenuDialog::SetLive(const char* name, const std::string& value) {
  if (rex::cvar::SetFlagByName(name, value)) {
    dirty_ = true;
    save_due_ = ImGui::GetTime() + 0.6;
    if (status_error_) status_.clear();
    status_error_ = false;
  } else {
    status_ = std::string("The game rejected ") + name + " = " + value + ".";
    status_error_ = true;
  }
}

void PortMenuDialog::SetNext(const char* name, const std::string& value) {
  if (next_[name] == value) return;
  next_[name] = value;
  dirty_ = true;
  save_due_ = ImGui::GetTime() + 0.6;
}

void PortMenuDialog::Set(const Setting& setting, const std::string& value) {
  if (setting.apply == Apply::kLive) {
    SetLive(setting.cvar, value);
  } else {
    SetNext(setting.cvar, value);
  }
  if (std::strcmp(setting.cvar, "port_perf_overlay") == 0 && host_->show_perf_overlay) {
    host_->show_perf_overlay(value == "true");
  }
}

bool PortMenuDialog::Save() {
  std::vector<std::pair<std::string, std::string>> values;
  for (const char* name : LiveKeys()) {
    // A launch flag the player did not touch here is not a saved preference.
    const auto source = rex::cvar::GetFlagSource(name);
    if (source == rex::cvar::Source::kCommandLine || source == rex::cvar::Source::kEnvironment) {
      if (Cvar(name) == open_live_[name]) continue;
    }
    if (!rex::cvar::GetFlagInfo(name)) continue;
    values.emplace_back(name, Cvar(name));
  }
  for (const auto& [name, value] : next_) {
    if (rex::cvar::GetFlagInfo(name)) values.emplace_back(name, value);
  }
  try {
    WritePortConfig(host_->config_path, values);
  } catch (const std::exception& e) {
    status_ = std::string("Could not save settings: ") + e.what();
    status_error_ = true;
    return false;
  }
  dirty_ = false;
  status_error_ = false;
  status_.clear();
  saved_at_ = ImGui::GetTime();
  return true;
}

bool PortMenuDialog::HasChangesSinceOpen() const {
  for (const auto& [name, value] : open_live_) {
    if (!SameValue(name.c_str(), Cvar(name.c_str()), value)) return true;
  }
  return next_ != open_next_;
}

void PortMenuDialog::Undo() {
  for (const auto& [name, value] : open_live_) {
    if (!SameValue(name.c_str(), Cvar(name.c_str()), value)) SetLive(name.c_str(), value);
  }
  for (const auto& [name, value] : open_next_) SetNext(name.c_str(), value);
  if (host_->show_perf_overlay) host_->show_perf_overlay(Cvar("port_perf_overlay") == "true");
  Load();
  next_ = open_next_;
  dirty_ = true;
  save_due_ = 0;
}

void PortMenuDialog::RequestClose() {
  if (SaveIfDirty()) Close();
}

void PortMenuDialog::OnClose() {
  if (on_closed_) on_closed_();
}

void PortMenuDialog::FeedGamepad(ImGuiIO& io) {
  const auto pads = port_input::ConnectedPads();
  if (pads.empty()) return;
  const port_input::PadSnapshot& pad = pads.front();
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  const auto button = [&](ImGuiKey key, uint16_t bit) { io.AddKeyEvent(key, (pad.buttons & bit) != 0); };
  button(ImGuiKey_GamepadDpadUp, 0x0001);
  button(ImGuiKey_GamepadDpadDown, 0x0002);
  button(ImGuiKey_GamepadDpadLeft, 0x0004);
  button(ImGuiKey_GamepadDpadRight, 0x0008);
  button(ImGuiKey_GamepadFaceDown, 0x1000);
  button(ImGuiKey_GamepadFaceRight, 0x2000);
  button(ImGuiKey_GamepadFaceLeft, 0x4000);
  button(ImGuiKey_GamepadFaceUp, 0x8000);
  const auto axis = [&](ImGuiKey key, float value) {
    const float v = std::clamp((value - 0.25f) / 0.75f, 0.0f, 1.0f);
    io.AddKeyAnalogEvent(key, v > 0.1f, v);
  };
  axis(ImGuiKey_GamepadLStickLeft, -pad.thumb_lx / 32767.0f);
  axis(ImGuiKey_GamepadLStickRight, pad.thumb_lx / 32767.0f);
  axis(ImGuiKey_GamepadLStickUp, pad.thumb_ly / 32767.0f);
  axis(ImGuiKey_GamepadLStickDown, -pad.thumb_ly / 32767.0f);

  // Bumpers switch tabs; Start closes (not while Back is held, which is the
  // chord that opens the menu).
  const uint16_t pressed = pad.buttons & ~last_pad_buttons_;
  last_pad_buttons_ = pad.buttons;
  if (pressed & 0x0100) pending_tab_ = (tab_ + kTabCount - 1) % kTabCount;
  if (pressed & 0x0200) pending_tab_ = (tab_ + 1) % kTabCount;
  if ((pressed & 0x0010) && !(pad.buttons & 0x0020)) RequestClose();
}

bool PortMenuDialog::BeginRows(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None)) return false;
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, kControlWidth);
  row_top_ = -1;
  return true;
}

void PortMenuDialog::Row(const char* label, const char* help, bool restart, bool pending) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  FinishRow(ImGui::GetCursorScreenPos().y);
  row_top_ = ImGui::GetCursorScreenPos().y;
  row_help_ = help ? help : "";
  if (restart) row_help_ += row_help_.empty() ? "Applies after a restart." : " Applies after a restart.";
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (pending) {
    ImGui::SameLine();
    ImGui::TextColored(kWarning, "restart");
  }
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

void PortMenuDialog::EndRows() {
  ImGui::EndTable();
  FinishRow(ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y);
}

// The help line follows the controller/keyboard cursor when it is showing,
// otherwise the mouse.
void PortMenuDialog::FinishRow(float bottom) {
  if (row_top_ < 0) return;
  const float top = row_top_;
  row_top_ = -1;
  if (row_help_.empty()) return;
  // GImGui is not exported from the runtime DLL; go through the context.
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* window = g.CurrentWindow;
  bool active = false;
  if (g.NavCursorVisible && g.NavId && g.NavWindow == window) {
    const ImRect nav = ImGui::WindowRectRelToAbs(window, window->NavRectRel[g.NavLayer]);
    const float y = nav.GetCenter().y;
    active = y >= top && y < bottom;
  } else if (ImGui::IsWindowHovered()) {
    const float y = ImGui::GetIO().MousePos.y;
    active = y >= top && y < bottom;
  }
  if (active) frame_help_ = row_help_;
}

void PortMenuDialog::DrawSetting(const Setting& setting) {
  if (setting.kind == Kind::kCustom) {
    DrawCustom(setting);
    return;
  }
  const bool live = setting.apply == Apply::kLive;
  const std::string value = live ? Cvar(setting.cvar) : next_[setting.cvar];
  const bool pending = !live && !SameValue(setting.cvar, running_[setting.cvar], value);
  Row(setting.label, setting.help, !live, pending);
  ImGui::PushID(setting.cvar);
  const auto* flag = rex::cvar::GetFlagInfo(setting.cvar);
  const bool is_double = flag && flag->type == rex::cvar::FlagType::Double;
  switch (setting.kind) {
    case Kind::kBool: {
      bool on = value == "true";
      if (ImGui::Checkbox("##v", &on)) Set(setting, on ? "true" : "false");
      break;
    }
    case Kind::kChoice:
      if (ImGui::BeginCombo("##v", ChoiceLabel(setting, value).c_str())) {
        for (const auto& choice : setting.choices) {
          const bool selected = SameValue(setting.cvar, choice.value, value);
          if (ImGui::Selectable(choice.label.c_str(), selected) && !selected) Set(setting, choice.value);
        }
        ImGui::EndCombo();
      }
      break;
    case Kind::kSegmented: {
      int index = -1;
      std::vector<std::string> labels;
      for (size_t i = 0; i < setting.choices.size(); ++i) {
        labels.push_back(setting.choices[i].label);
        if (SameValue(setting.cvar, setting.choices[i].value, value)) index = int(i);
      }
      if (Segmented("##v", index, labels)) Set(setting, setting.choices[index].value);
      break;
    }
    case Kind::kInt: {
      int number = std::atoi(value.c_str());
      if (ImGui::SliderInt("##v", &number, setting.min, setting.max)) Set(setting, std::to_string(number));
      break;
    }
    case Kind::kPercent: {
      double number = 0;
      rex::cvar::ParseDouble(value, number);
      int percent = int(std::lround(is_double ? number * 100 : number));
      if (ImGui::SliderInt("##v", &percent, setting.min, setting.max, "%d%%")) {
        Set(setting, is_double ? std::to_string(percent / 100.0) : std::to_string(percent));
      }
      break;
    }
    case Kind::kCustom:
      break;
  }
  ImGui::PopID();
}

void PortMenuDialog::DrawCustom(const Setting& setting) {
  const std::string id = setting.cvar;
  if (id == "window_size") {
    Row(setting.label, setting.help);
    const PortWindowInfo window = host_->window_info ? host_->window_info() : PortWindowInfo{};
    const std::string current = std::to_string(window.width) + " x " + std::to_string(window.height);
    if (ImGui::BeginCombo("##window_size", current.c_str())) {
      const int screen_w = GetSystemMetrics(SM_CXSCREEN), screen_h = GetSystemMetrics(SM_CYSCREEN);
      for (const auto& [w, h] : kWindowSizes) {
        if (int(w) > screen_w || int(h) > screen_h) continue;
        const std::string label = std::to_string(w) + " x " + std::to_string(h);
        if (ImGui::Selectable(label.c_str(), w == window.width && h == window.height)) {
          if (host_->resize_window) host_->resize_window(w, h);
          SetLive("window_width", std::to_string(w));
          SetLive("window_height", std::to_string(h));
        }
      }
      ImGui::EndCombo();
    }
  } else if (id == "reset_mapping") {
    Row("", "Every button sends itself again.");
    if (ImGui::Button("Reset buttons", ImVec2(-FLT_MIN, 0))) {
      for (int i = 0; i < 8; ++i) SetLive(port_input::kMapCvars[i], port_input::kControlNames[i]);
    }
  } else if (id == "key_bindings") {
    for (int i = 0; i < 4; ++i) {
      Row(kKeyLabels[i], "Key names separated by commas, for example Space or Semicolon,Space.");
      ImGui::PushID(i);
      ImGui::InputText("##key", keys_[i], sizeof(keys_[i]));
      if (ImGui::IsItemDeactivatedAfterEdit()) SetLive(kKeyFlags[i], keys_[i]);
      ImGui::PopID();
    }
  } else if (id == "csv_log") {
    const auto csv = host_->sampler ? host_->sampler->csv_path() : std::filesystem::path();
    std::string help = setting.help;
    if (!csv.empty()) help = "Writing " + csv.string();
    Row(setting.label, help.c_str());
    if (ImGui::Button(csv.empty() ? "Start" : "Stop", ImVec2(-FLT_MIN, 0)) && host_->set_csv_logging) {
      host_->set_csv_logging(csv.empty());
    }
  } else if (id == "profiler") {
    if (!host_->profiler) return;
    Row(setting.label, setting.help);
    const telemetry::ProfileReport report = host_->profiler->Report();
    const auto system = host_->sampler ? host_->sampler->Latest() : telemetry::SystemSnapshot{};
    ImGui::BeginDisabled(report.running || system.busiest_threads.empty());
    if (ImGui::Button(report.running ? "Profiling..." : "Run", ImVec2(-FLT_MIN, 0))) {
      std::vector<std::pair<uint32_t, std::string>> threads;
      for (const auto& thread : system.busiest_threads) {
        if (threads.size() < 4 && thread.core_percent >= 5) threads.emplace_back(thread.id, thread.name);
      }
      host_->profiler->Start(std::move(threads), 5.0, host_->log_dir);
    }
    ImGui::EndDisabled();
  } else if (id == "reset_advanced") {
    Row("", "Puts every setting on this page back to its default.");
    if (ImGui::Button("Reset to defaults", ImVec2(-FLT_MIN, 0))) {
      for (const auto& group : settings_catalog::Groups()) {
        if (std::strcmp(group.tab, "ADVANCED") != 0) continue;
        for (const auto& s : group.settings) {
          const auto* flag = s.kind == Kind::kCustom ? nullptr : rex::cvar::GetFlagInfo(s.cvar);
          if (flag) Set(s, flag->default_value);
        }
      }
    }
  }
}

void PortMenuDialog::DrawGroup(const settings_catalog::Group& group) {
  // Backend-specific settings follow the API that is running now.
  const auto running = running_.find("port_gpu_backend");
  const std::string backend = running != running_.end() ? running->second : "d3d12";
  const settings_catalog::ValueOf value_of = [this](const char* name) { return ValueOf(name); };
  std::vector<const Setting*> visible;
  for (const auto& setting : group.settings) {
    if (setting.kind != Kind::kCustom && !rex::cvar::GetFlagInfo(setting.cvar)) continue;
    if (setting.backend && backend != setting.backend) continue;
    if ((setting.kind == Kind::kChoice || setting.kind == Kind::kSegmented) &&
        setting.choices.size() < 2) {
      continue;
    }
    if (setting.visible && !setting.visible(value_of)) continue;
    visible.push_back(&setting);
  }
  if (visible.empty()) return;
  GroupTitle(group.title);
  if (!BeginRows(group.title)) return;
  for (const auto* setting : visible) DrawSetting(*setting);
  EndRows();
}

void PortMenuDialog::DrawPerformanceHeader() {
  const telemetry::FrameStats now = telemetry::ComputeFrameStats(1.0);
  const telemetry::FrameStats recent = telemetry::ComputeFrameStats(5.0);
  const auto system = host_->sampler ? host_->sampler->Latest() : telemetry::SystemSnapshot{};
  ImGui::PushFont(nullptr, 30.0f);
  ImGui::TextColored(now.fps >= 57 ? kOk : kWarning, "%.0f FPS", now.fps);
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::Text("%.1f ms   1%% low %.1f ms   worst %.1f ms", recent.avg_ms, recent.p99_ms, recent.max_ms);
  const auto hints = DiagnosePerformance(recent, system);
  ImGui::PushTextWrapPos(0.0f);
  if (!hints.empty()) ImGui::TextColored(hints.front().warning ? kWarning : kDim, "%s", hints.front().text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  telemetry::RecentFrameTimes(frame_times_, 240);
  DrawFrameTimeGraph(frame_times_, ImGui::GetContentRegionAvail().x, 40.0f);
}

void PortMenuDialog::DrawProfileReport() {
  if (!host_->profiler) return;
  const telemetry::ProfileReport report = host_->profiler->Report();
  if (report.threads.empty()) return;
  GroupTitle("PROFILE");
  ImGui::TextColored(kDim, "%s", report.summary.c_str());
  for (const auto& thread : report.threads) {
    ImGui::TextColored(kBrass, "%s", thread.thread_name.c_str());
    for (size_t i = 0; i < thread.top.size() && i < 4; ++i) {
      ImGui::TextColored(kDim, "  %5.1f%%  %s", thread.top[i].percent, thread.top[i].location.c_str());
    }
  }
}

void PortMenuDialog::DrawControllerHeader() {
  const auto pads = port_input::ConnectedPads();
  if (pads.empty()) {
    ImGui::TextColored(kDim, "No controller detected. Press a button to wake it.");
    return;
  }
  const auto& pad = pads.front();
  DrawStick(pad.thumb_lx, pad.thumb_ly, rex::cvar::Query<int32_t>("pad_left_deadzone"));
  ImGui::SameLine();
  DrawStick(pad.thumb_rx, pad.thumb_ry, rex::cvar::Query<int32_t>("pad_right_deadzone"));
  ImGui::SameLine(0, 16);
  ImGui::BeginGroup();
  ImGui::Text("%s", pad.name.empty() ? "Controller" : pad.name.c_str());
  ImGui::ProgressBar(pad.left_trigger / 255.0f, ImVec2(90, 6), "");
  ImGui::SameLine();
  ImGui::ProgressBar(pad.right_trigger / 255.0f, ImVec2(90, 6), "");
  const std::string pressed = PressedButtons(pad);
  ImGui::TextColored(kDim, "%s", pressed.empty() ? " " : pressed.c_str());
  ImGui::EndGroup();
}

void PortMenuDialog::DrawAbout() {
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextColored(kBrass, "MKVDCU-Recomp %s", Cvar("port_version").c_str());
  ImGui::TextColored(kDim, "Mortal Kombat vs. DC Universe, recompiled for Windows.");
  if (host_->mods_loaded) {
    ImGui::TextColored(kOk, "%zu mod%s active", host_->mods_loaded, host_->mods_loaded == 1 ? "" : "s");
  }
  GroupTitle("CONTROLS");
  if (BeginRows("about_controls")) {
    const std::pair<const char*, const char*> kKeys[] = {
        {"Open or close this menu", "F1, or hold Back + Start"},
        {"Switch tabs", "LB / RB, or Page Up / Page Down"},
        {"Performance overlay", "F2"},
    };
    for (const auto& [what, keys] : kKeys) {
      Row(what, nullptr);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kDim, "%s", keys);
    }
    EndRows();
  }
  GroupTitle("FILES");
  ImGui::TextColored(kDim, "Settings save automatically to %s", host_->config_path.string().c_str());
  if (ImGui::Button("Open settings folder")) {
    ShellExecuteW(nullptr, L"open", host_->config_path.parent_path().wstring().c_str(), nullptr,
                  nullptr, SW_SHOWNORMAL);
  }
}

void PortMenuDialog::DrawTab(const char* tab) {
  const std::string name = tab;
  if (name == "ABOUT") {
    DrawAbout();
    return;
  }
  if (name == "PERFORMANCE") DrawPerformanceHeader();
  if (name == "CONTROLS") DrawControllerHeader();
  if (name == "ADVANCED") {
    ImGui::TextColored(kWarning, "These defaults are tested. Change them only to troubleshoot.");
  }
  for (const auto& group : settings_catalog::Groups()) {
    if (name == group.tab) DrawGroup(group);
  }
  if (name == "DISPLAY") {
    const telemetry::GuestOutputSize size = telemetry::LastGuestOutputSize();
    const auto [scale_x, scale_y] = host_tweaks::RenderScale();
    const PortWindowInfo window = host_->window_info ? host_->window_info() : PortWindowInfo{};
    ImGui::Dummy(ImVec2(0, 4));
    if (size.width) {
      ImGui::TextColored(kDim, "Game %u x %u    Rendering %u x %u    Window %u x %u", size.width,
                         size.height, size.width * scale_x, size.height * scale_y, window.width,
                         window.height);
    }
  }
  if (name == "PERFORMANCE") DrawProfileReport();
}

void PortMenuDialog::DrawFooter() {
  int pending = 0;
  for (const auto& [name, value] : next_) {
    if (!SameValue(name.c_str(), running_[name], value)) ++pending;
  }
  std::string status;
  const ImVec4* color = &kDim;
  if (status_error_) {
    status = status_;
    color = &kError;
  } else if (pending) {
    status = "Restart the game to apply " + std::to_string(pending) +
             (pending == 1 ? " change" : " changes");
    color = &kWarning;
  } else if (ImGui::GetTime() - saved_at_ < 2.0) {
    status = "Saved";
    color = &kOk;
  }

  const bool can_undo = HasChangesSinceOpen();
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  const char* undo_label = "Undo changes";
  const float undo_width = ImGui::CalcTextSize(undo_label).x + ImGui::GetStyle().FramePadding.x * 2;
  const float status_width = status.empty() ? 0 : ImGui::CalcTextSize(status.c_str()).x + 16;
  const float help_width = ImGui::GetContentRegionAvail().x - undo_width - status_width - 16;

  const std::string help = help_.empty()
                               ? "LB / RB switch tabs.   Start or F1 closes.   Settings save automatically."
                               : help_;
  ImGui::AlignTextToFramePadding();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + help_width);
  ImGui::TextColored(kDim, "%s", help.c_str());
  ImGui::PopTextWrapPos();
  if (!status.empty()) {
    ImGui::SameLine(right - undo_width - status_width);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(*color, "%s", status.c_str());
  }
  ImGui::SameLine(right - undo_width);
  ImGui::BeginDisabled(!can_undo);
  if (ImGui::Button(undo_label)) Undo();
  ImGui::EndDisabled();
}

void PortMenuDialog::OnDraw(ImGuiIO& io) {
  FeedGamepad(io);
  if (dirty_ && ImGui::GetTime() >= save_due_) Save();

  const ImVec2 display = io.DisplaySize;
  auto* back = ImGui::GetBackgroundDrawList();
  back->AddRectFilled(ImVec2(0, 0), display, IM_COL32(6, 7, 9, 204));
  const float width = std::min(display.x - 32.0f, 900.0f);
  const float height = std::min(display.y - 28.0f, 640.0f);
  ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f, (display.y - height) * 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  for (const auto& c : kPalette) ImGui::PushStyleColor(c.index, c.color);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 8));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 5));
  const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("MKVDCU-RECOMP##port_menu", nullptr, flags)) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    draw->AddRectFilled(min, ImVec2(min.x + width / 2, min.y + 4), kRed);
    draw->AddRectFilled(ImVec2(min.x + width / 2, min.y), ImVec2(min.x + width, min.y + 4), kBlue);

    const char* close_label = "CLOSE";
    const float close_width =
        ImGui::CalcTextSize(close_label).x + ImGui::GetStyle().FramePadding.x * 2;
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kBrass, "MKVDCU-RECOMP");
    ImGui::SameLine(right - close_width);
    if (ImGui::Button(close_label)) RequestClose();

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 2;
    if (const int cycle = REXCVAR_GET(port_debug_menu_cycle); cycle > 0 && pending_tab_ < 0) {
      const int tab = int(ImGui::GetTime() / cycle) % kTabCount;
      if (tab != tab_) pending_tab_ = tab;
    }
    if (!io.WantTextInput && pending_tab_ < 0) {
      if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) pending_tab_ = (tab_ + kTabCount - 1) % kTabCount;
      if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) pending_tab_ = (tab_ + 1) % kTabCount;
    }
    frame_help_.clear();
    if (ImGui::BeginTabBar("port_tabs")) {
      for (int i = 0; i < kTabCount; ++i) {
        const ImGuiTabItemFlags tab_flags = pending_tab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(kTabs[i], nullptr, tab_flags)) {
          tab_ = i;
          ImGui::BeginChild("settings", ImVec2(0, -footer));
          DrawTab(kTabs[i]);
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      }
      pending_tab_ = -1;
      ImGui::EndTabBar();
    }
    // Keep the last help while a dropdown is open over its row.
    if (!frame_help_.empty() || !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) help_ = frame_help_;
    ImGui::Separator();
    DrawFooter();
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(static_cast<int>(std::size(kPalette)));
}
