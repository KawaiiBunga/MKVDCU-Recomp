#include "port_menu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <imgui.h>
#include <rex/cvar.h>

#include "input/controller_filter.h"
#include "port_menu/port_config.h"
#include "port_menu/settings_catalog.h"
#include "telemetry/frame_telemetry.h"
#include "telemetry/guest_profiler.h"
#include "telemetry/perf_overlay.h"
#include "telemetry/system_telemetry.h"

namespace {
constexpr ImU32 kRed = IM_COL32(142, 59, 55, 255);
constexpr ImU32 kBlue = IM_COL32(55, 82, 106, 255);
constexpr ImVec4 kBrass(0.73f, 0.64f, 0.49f, 1.0f);
constexpr ImVec4 kWarning(0.85f, 0.50f, 0.38f, 1.0f);
constexpr ImVec4 kError(0.92f, 0.38f, 0.34f, 1.0f);
constexpr ImVec4 kOk(0.55f, 0.72f, 0.56f, 1.0f);
constexpr ImVec4 kSelected(0.58f, 0.40f, 0.27f, 1.0f);

// Settings drawn by custom widgets that the running game picks up as soon as
// their cvar changes. The catalog adds its own (settings_catalog.cpp).
constexpr const char* kMenuLiveKeys[] = {
    "fullscreen",        "window_width",      "window_height",        "present_letterbox",
    "vsync",             "anisotropic_override", "async_shader_compilation", "mnk_mode",
    "keybind_a",         "keybind_b",         "keybind_x",            "keybind_y",
    "pad_left_deadzone", "pad_right_deadzone", "pad_trigger_deadzone", "pad_vibration",
    "pad_stick_to_dpad", "pad_menu_chord",    "pad_map_a",            "pad_map_b",
    "pad_map_x",         "pad_map_y",         "pad_map_lb",           "pad_map_rb",
    "pad_map_lt",        "pad_map_rt",        "port_perf_overlay",    "port_perf_detail",
    "port_perf_corner"};
// Custom-widget settings only read while the game starts.
constexpr const char* kMenuRestartKeys[] = {"resolution_scale", "swap_post_effect",
                                            "video_mode_refresh_rate",
                                            "d3d12_allow_variable_refresh_rate_and_tearing",
                                            "monitor"};

std::vector<const char*> LiveKeys() {
  std::vector<const char*> keys(std::begin(kMenuLiveKeys), std::end(kMenuLiveKeys));
  for (const char* name : settings_catalog::LiveCvars()) keys.push_back(name);
  return keys;
}

std::vector<const char*> RestartKeys() {
  std::vector<const char*> keys(std::begin(kMenuRestartKeys), std::end(kMenuRestartKeys));
  for (const char* name : settings_catalog::RestartCvars()) keys.push_back(name);
  return keys;
}

constexpr const char* kKeyFlags[] = {"keybind_a", "keybind_b", "keybind_x", "keybind_y"};
constexpr const char* kKeyLabels[] = {"A button", "B button", "X button", "Y button"};
constexpr const char* kPadLabels[] = {"A button", "B button", "X button", "Y button",
                                      "Left bumper", "Right bumper", "Left trigger",
                                      "Right trigger"};

std::string Cvar(const char* name) {
  return rex::cvar::GetFlagByName(name);
}
bool CvarBool(const char* name) {
  return Cvar(name) == "true";
}
int CvarInt(const char* name) {
  return rex::cvar::Query<int32_t>(name);
}

// Same conversion rex::cvar::LoadConfig applies to a TOML value.
std::optional<std::string> NodeString(const toml::node& node) {
  if (const auto* v = node.as_boolean()) return v->get() ? "true" : "false";
  if (const auto* v = node.as_integer()) return std::to_string(v->get());
  if (const auto* v = node.as_floating_point()) return std::to_string(v->get());
  if (const auto* v = node.as_string()) return v->get();
  return std::nullopt;
}

std::string AntiAliasingName(const std::string& value) {
  if (value == "fxaa") return "FXAA";
  if (value == "fxaa_extreme") return "FXAA extreme";
  return "Off";
}

std::string RefreshName(const std::string& value) {
  double hz = 60;
  rex::cvar::ParseDouble(value, hz);
  char text[32];
  std::snprintf(text, sizeof(text), "%.0f Hz", hz);
  return text;
}

std::string MonitorName(int index) {
  if (index <= 0) return "Automatic";
  if (index == 1) return "Primary display";
  return "Display " + std::to_string(index);
}

constexpr std::pair<int, const char*> kAnisotropic[] = {
    {-1, "Game default"}, {0, "Off"}, {1, "1x"}, {2, "2x"}, {3, "4x"}, {4, "8x"}, {5, "16x"}};

constexpr std::pair<uint32_t, uint32_t> kWindowSizes[] = {
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};

void Group(const char* title, const char* detail, const ImVec4& color) {
  ImGui::Dummy(ImVec2(0, 2));
  ImGui::TextColored(color, "%s", title);
  if (detail) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", detail);
  }
  ImGui::Separator();
}

void Live() {
  Group("APPLIES NOW", "Takes effect immediately.", kOk);
}
void NextLaunch() {
  ImGui::Dummy(ImVec2(0, 8));
  Group("NEXT LAUNCH", "Saved now, used after you restart the game.", kWarning);
}

bool BeginRows(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None)) return false;
  ImGui::TableSetupColumn("setting", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 320.0f);
  return true;
}

void Hint(const char* text, const ImVec4* color = nullptr) {
  ImGui::PushStyleColor(ImGuiCol_Text,
                        color ? *color : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void Row(const char* label, const char* hint) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextUnformatted(label);
  if (hint) Hint(hint);
  ImGui::TableNextColumn();
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::SetNextItemWidth(-FLT_MIN);
}

void RestartState(const std::string& running, const std::string& next) {
  if (running == next) {
    ImGui::TextDisabled("In use now");
  } else {
    const std::string text = "Now " + running + ". " + next + " after restart.";
    Hint(text.c_str(), &kWarning);
  }
}

bool Segmented(const char* id, int& index, const char* const* labels, int count) {
  bool changed = false;
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
    if (ImGui::Button(labels[i], ImVec2(width, 0)) && !selected) {
      index = i;
      changed = true;
    }
    if (selected) ImGui::PopStyleColor(2);
  }
  ImGui::PopID();
  return changed;
}

BOOL CALLBACK CountMonitor(HMONITOR, HDC, LPRECT, LPARAM data) {
  ++*reinterpret_cast<int*>(data);
  return TRUE;
}

// Stick position with its deadzone ring, as the game will see it.
void DrawStick(const char* label, int16_t x, int16_t y, int deadzone_percent) {
  const float radius = 30.0f;
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
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::TextUnformatted(label);
  ImGui::TextDisabled("%+.2f  %+.2f", x / 32767.0f, y / 32767.0f);
  ImGui::EndGroup();
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
  return text.empty() ? "none" : text;
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
  EnumDisplayMonitors(nullptr, nullptr, CountMonitor, reinterpret_cast<LPARAM>(&monitor_count_));
  monitor_count_ = std::max(monitor_count_, 1);
  // A button still held from the chord that opened the menu is not a press.
  const auto pads = port_input::ConnectedPads();
  if (!pads.empty()) last_pad_buttons_ = pads.front().buttons;
  if (host_->sampler) host_->sampler->SetWanted("menu", true);
  Load();
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
  saved_live_.clear();
  for (const char* name : LiveKeys()) saved_live_.emplace_back(name, Cvar(name));

  running_.clear();
  next_.clear();
  for (const char* name : RestartKeys()) {
    running_[name] = Cvar(name);
    next_[name] = running_[name];
  }
  if (const auto config = ReadPortConfig(host_->config_path)) {
    for (const char* name : RestartKeys()) {
      if (const auto* node = config->get(name)) {
        if (auto value = NodeString(*node)) next_[name] = *value;
      }
    }
  }
  dirty_ = false;
}

void PortMenuDialog::SetLive(const char* name, const std::string& value) {
  if (rex::cvar::SetFlagByName(name, value)) {
    dirty_ = true;
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
}

bool PortMenuDialog::Save() {
  std::vector<std::pair<std::string, std::string>> values;
  for (const char* name : LiveKeys()) {
    // A launch flag the player did not touch here is not a saved preference.
    const auto source = rex::cvar::GetFlagSource(name);
    if (source == rex::cvar::Source::kCommandLine || source == rex::cvar::Source::kEnvironment) {
      continue;
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
    status_ = std::string("Not saved: ") + e.what();
    status_error_ = true;
    return false;
  }
  saved_live_.clear();
  for (const char* name : LiveKeys()) saved_live_.emplace_back(name, Cvar(name));
  dirty_ = false;
  status_error_ = false;
  status_.clear();
  return true;
}

void PortMenuDialog::Revert() {
  const bool overlay_was = CvarBool("port_perf_overlay");
  for (const auto& [name, value] : saved_live_) rex::cvar::SetFlagByName(name, value);
  if (CvarBool("port_perf_overlay") != overlay_was && host_->show_perf_overlay) {
    host_->show_perf_overlay(CvarBool("port_perf_overlay"));
  }
  Load();
  status_error_ = false;
  status_ = "Changes undone.";
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
  constexpr int kTabCount = 7;
  if (pressed & 0x0100) pending_tab_ = (tab_ + kTabCount - 1) % kTabCount;
  if (pressed & 0x0200) pending_tab_ = (tab_ + 1) % kTabCount;
  if ((pressed & 0x0010) && !(pad.buttons & 0x0020)) RequestClose();
}

void PortMenuDialog::DrawDisplay() {
  Live();
  if (BeginRows("display_live")) {
    Row("Display mode", "Borderless fullscreen covers the display; with Direct3D 12 it presents "
                        "as directly as exclusive fullscreen would.");
    static const char* kModes[] = {"Windowed", "Borderless fullscreen"};
    int mode = CvarBool("fullscreen") ? 1 : 0;
    if (Segmented("mode", mode, kModes, 2)) SetLive("fullscreen", mode ? "true" : "false");

    if (!CvarBool("fullscreen")) {
      Row("Window size", "Client area in pixels. Also used for the next launch.");
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
    }

    Row("Keep 16:9", "Black bars instead of stretching when the window has another shape.");
    bool letterbox = CvarBool("present_letterbox");
    if (ImGui::Checkbox("##letterbox", &letterbox)) SetLive("present_letterbox", letterbox ? "true" : "false");
    ImGui::EndTable();
  }

  NextLaunch();
  if (BeginRows("display_restart")) {
    Row("Display", "Which monitor the game opens on.");
    const int next_monitor = std::atoi(next_["monitor"].c_str());
    if (ImGui::BeginCombo("##monitor", MonitorName(next_monitor).c_str())) {
      for (int i = 0; i <= monitor_count_; ++i) {
        if (ImGui::Selectable(MonitorName(i).c_str(), i == next_monitor)) {
          SetNext("monitor", std::to_string(i));
        }
      }
      ImGui::EndCombo();
    }
    RestartState(MonitorName(std::atoi(running_["monitor"].c_str())), MonitorName(next_monitor));

    Row("Internal render scale",
        "Renders at a multiple of the game's resolution. 1x is the verified default; higher "
        "scales cost GPU time and memory.");
    static const char* kScales[] = {"1x", "2x", "3x"};
    int scale = std::clamp(std::atoi(next_["resolution_scale"].c_str()), 1, 3) - 1;
    if (Segmented("scale", scale, kScales, 3)) SetNext("resolution_scale", std::to_string(scale + 1));
    RestartState(running_["resolution_scale"] + "x", next_["resolution_scale"] + "x");
    ImGui::EndTable();
  }

  ImGui::Dummy(ImVec2(0, 8));
  Group("RESOLUTION", "Measured from the running game.", kBrass);
  if (BeginRows("display_info")) {
    const telemetry::GuestOutputSize size = telemetry::LastGuestOutputSize();
    const int scale = std::max(1, std::atoi(running_["resolution_scale"].c_str()));
    Row("Game output", "The image the game presents each frame.");
    if (size.width) {
      ImGui::Text("%u x %u", size.width, size.height);
    } else {
      ImGui::TextDisabled("waiting for a frame");
    }
    Row("Internal render", "Game output times the render scale in use.");
    ImGui::Text("%u x %u", size.width * scale, size.height * scale);
    if (host_->window_info) {
      const PortWindowInfo window = host_->window_info();
      Row("Window", "What the render is scaled to for display.");
      ImGui::Text("%u x %u", window.width, window.height);
    }
    ImGui::EndTable();
  }
}

void PortMenuDialog::DrawGraphics() {
  Live();
  if (BeginRows("graphics_live")) {
    Row("Anisotropic filtering", "Keeps floor and wall textures sharp at steep angles.");
    const int aniso = CvarInt("anisotropic_override");
    const char* current = "Custom";
    for (const auto& [value, label] : kAnisotropic) {
      if (value == aniso) current = label;
    }
    if (ImGui::BeginCombo("##aniso", current)) {
      for (const auto& [value, label] : kAnisotropic) {
        if (ImGui::Selectable(label, value == aniso) && value != aniso) {
          SetLive("anisotropic_override", std::to_string(value));
        }
      }
      ImGui::EndCombo();
    }

    Row("Background shader compilation",
        "Builds new shaders on worker threads so the game does not freeze on them. Objects can "
        "be missing for a frame while that happens. Off gives exact frames with more hitches.");
    bool async = CvarBool("async_shader_compilation");
    if (ImGui::Checkbox("##async", &async)) SetLive("async_shader_compilation", async ? "true" : "false");
    ImGui::EndTable();
  }

  NextLaunch();
  if (BeginRows("graphics_restart")) {
    Row("Anti-aliasing", "FXAA smooths jagged edges after the frame is drawn, for a small GPU "
                         "cost. It also slightly softens the image.");
    static const char* kAaLabels[] = {"Off", "FXAA", "FXAA extreme"};
    static const char* kAaValues[] = {"none", "fxaa", "fxaa_extreme"};
    int aa = 0;
    for (int i = 0; i < 3; ++i) {
      if (next_["swap_post_effect"] == kAaValues[i]) aa = i;
    }
    if (Segmented("aa", aa, kAaLabels, 3)) SetNext("swap_post_effect", kAaValues[aa]);
    RestartState(AntiAliasingName(running_["swap_post_effect"]), AntiAliasingName(next_["swap_post_effect"]));
    ImGui::EndTable();
  }
  DrawCatalog("GRAPHICS");
}

void PortMenuDialog::DrawCatalog(const char* tab) {
  using settings_catalog::Apply;
  using Kind = settings_catalog::Setting::Kind;
  const std::string backend = running_.count("port_gpu_backend") ? running_["port_gpu_backend"] : "d3d12";

  for (const auto& group : settings_catalog::Groups()) {
    if (std::strcmp(group.tab, tab) != 0) continue;
    std::vector<const settings_catalog::Setting*> visible;
    for (const auto& setting : group.settings) {
      if (!rex::cvar::GetFlagInfo(setting.cvar)) continue;  // not in this build
      if (setting.backend && backend != setting.backend) continue;
      if (setting.kind == Kind::kChoice && setting.choices.size() < 2) continue;
      visible.push_back(&setting);
    }
    if (visible.empty()) continue;

    ImGui::Dummy(ImVec2(0, 8));
    Group(group.title, group.detail, kBrass);
    if (!BeginRows(group.title)) continue;
    for (const auto* setting : visible) {
      const bool live = setting->apply == Apply::kLive;
      const std::string value = live ? Cvar(setting->cvar) : next_[setting->cvar];
      const auto set = [&](const std::string& v) {
        if (live) {
          SetLive(setting->cvar, v);
        } else {
          SetNext(setting->cvar, v);
        }
      };
      const auto* flag = rex::cvar::GetFlagInfo(setting->cvar);
      const bool is_double = flag && flag->type == rex::cvar::FlagType::Double;
      const auto label_of = [&](const std::string& v) -> std::string {
        switch (setting->kind) {
          case Kind::kBool:
            return v == "true" ? "On" : "Off";
          case Kind::kChoice:
            for (const auto& choice : setting->choices) {
              if (choice.value == v) return choice.label;
            }
            return v.empty() ? "Automatic" : v;
          case Kind::kPercent: {
            double number = 0;
            rex::cvar::ParseDouble(v, number);
            return std::to_string(int(std::lround(is_double ? number * 100 : number))) + "%";
          }
          default:
            return v;
        }
      };

      Row(setting->label, setting->hint);
      ImGui::PushID(setting->cvar);
      switch (setting->kind) {
        case Kind::kBool: {
          bool on = value == "true";
          if (ImGui::Checkbox("##v", &on)) set(on ? "true" : "false");
          break;
        }
        case Kind::kChoice:
          if (ImGui::BeginCombo("##v", label_of(value).c_str())) {
            for (const auto& choice : setting->choices) {
              if (ImGui::Selectable(choice.label.c_str(), choice.value == value)) set(choice.value);
            }
            ImGui::EndCombo();
          }
          break;
        case Kind::kInt: {
          int number = std::atoi(value.c_str());
          if (ImGui::SliderInt("##v", &number, setting->min, setting->max)) set(std::to_string(number));
          break;
        }
        case Kind::kPercent: {
          double number = 0;
          rex::cvar::ParseDouble(value, number);
          int percent = int(std::lround(is_double ? number * 100 : number));
          if (ImGui::SliderInt("##v", &percent, setting->min, setting->max, "%d%%")) {
            set(is_double ? std::to_string(percent / 100.0) : std::to_string(percent));
          }
          break;
        }
      }
      if (!live) RestartState(label_of(running_[setting->cvar]), label_of(value));
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void PortMenuDialog::DrawAudio() {
  DrawCatalog("AUDIO");
  ImGui::Dummy(ImVec2(0, 4));
  Hint("The mix settings shape how the game's 5.1 output is folded into stereo. Output device "
       "selection follows the Windows default device.");
}

void PortMenuDialog::DrawSystem() {
  DrawCatalog("SYSTEM");
  ImGui::Dummy(ImVec2(0, 8));
  Group("PROCESS", nullptr, kBrass);
  const auto system = host_->sampler ? host_->sampler->Latest() : telemetry::SystemSnapshot{};
  if (BeginRows("process")) {
    Row("CPU threads", nullptr);
    ImGui::Text("%u logical", system.logical_cores ? system.logical_cores
                                                   : std::max(1u, std::thread::hardware_concurrency()));
    Row("Graphics", nullptr);
    ImGui::Text("%s", system.gpu_name.empty() ? "measuring..." : system.gpu_name.c_str());
    Row("Settings file", nullptr);
    Hint(host_->config_path.string().c_str());
    ImGui::EndTable();
  }
}

void PortMenuDialog::DrawPerformance() {
  const telemetry::FrameStats now = telemetry::ComputeFrameStats(1.0);
  const telemetry::FrameStats recent = telemetry::ComputeFrameStats(5.0);
  const telemetry::SystemSnapshot system =
      host_->sampler ? host_->sampler->Latest() : telemetry::SystemSnapshot{};

  Group("RIGHT NOW", "Measured at the game's own frame presentation.", kBrass);
  ImGui::PushFont(nullptr, 30.0f);
  ImGui::TextColored(now.fps >= 57 ? kOk : kWarning, "%.0f FPS", now.fps);
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::Text("%.2f ms average   1%% low %.2f ms   worst %.2f ms", recent.avg_ms, recent.p99_ms,
              recent.max_ms);
  ImGui::TextDisabled("guest vblank %.1f Hz   jitter %.2f ms   hitches (5 s) %u", system.vblank_hz,
                      recent.stddev_ms, recent.hitches);
  ImGui::EndGroup();
  telemetry::RecentFrameTimes(frame_times_, 240);
  DrawFrameTimeGraph(frame_times_, ImGui::GetContentRegionAvail().x, 50.0f);

  ImGui::Dummy(ImVec2(0, 6));
  Group("FRAME PACING", nullptr, kBrass);
  if (BeginRows("pacing_live")) {
    Row("Game timing",
        "The game advances one step per 60 Hz vertical blank, like the console. Unlocked "
        "replaces that with a 1000 Hz timer: the game runs as fast as your PC allows, and "
        "gameplay speeds up with it. Use it only to measure headroom.");
    static const char* kTiming[] = {"Console 60 Hz", "Unlocked"};
    int timing = CvarBool("vsync") ? 0 : 1;
    if (Segmented("timing", timing, kTiming, 2)) SetLive("vsync", timing ? "false" : "true");
    if (timing) Hint("Unlocked: gameplay, animation and music sync run faster than normal.", &kWarning);
    ImGui::EndTable();
  }
  NextLaunch();
  if (BeginRows("pacing_restart")) {
    Row("Guest refresh rate",
        "The display rate the game is told it runs at. Anything but 60 Hz changes game speed "
        "the same way Unlocked does.");
    static const char* kRates[] = {"60.000000", "50.000000", "120.000000", "144.000000"};
    if (ImGui::BeginCombo("##refresh", RefreshName(next_["video_mode_refresh_rate"]).c_str())) {
      for (const char* rate : kRates) {
        std::string label = RefreshName(rate);
        if (std::string(rate) == "60.000000") label += " (console)";
        else label += " (experimental)";
        double next = 0, value = 0;
        rex::cvar::ParseDouble(next_["video_mode_refresh_rate"], next);
        rex::cvar::ParseDouble(rate, value);
        if (ImGui::Selectable(label.c_str(), std::abs(next - value) < 0.01)) {
          SetNext("video_mode_refresh_rate", rate);
        }
      }
      ImGui::EndCombo();
    }
    RestartState(RefreshName(running_["video_mode_refresh_rate"]),
                 RefreshName(next_["video_mode_refresh_rate"]));

    Row("Allow tearing / variable refresh",
        "Shows each frame the moment it is ready. With G-Sync or FreeSync this lowers latency "
        "without tearing; on a fixed-rate display it can tear.");
    bool tearing = next_["d3d12_allow_variable_refresh_rate_and_tearing"] == "true";
    if (ImGui::Checkbox("##tearing", &tearing)) {
      SetNext("d3d12_allow_variable_refresh_rate_and_tearing", tearing ? "true" : "false");
    }
    RestartState(running_["d3d12_allow_variable_refresh_rate_and_tearing"] == "true" ? "On" : "Off",
                 tearing ? "On" : "Off");
    ImGui::EndTable();
  }
  ImGui::Dummy(ImVec2(0, 4));
  Hint("Frame interpolation (inserting generated frames between the game's own) is not "
       "available: the renderer receives finished frames without motion vectors or depth "
       "history to build them from. See About for the frame-rate findings.");

  ImGui::Dummy(ImVec2(0, 8));
  Group("PERFORMANCE OVERLAY", "F2 toggles it at any time.", kBrass);
  if (BeginRows("overlay")) {
    Row("Show overlay", "Frame rate, frame times and load on top of the game.");
    bool overlay = CvarBool("port_perf_overlay");
    if (ImGui::Checkbox("##overlay", &overlay)) {
      SetLive("port_perf_overlay", overlay ? "true" : "false");
      if (host_->show_perf_overlay) host_->show_perf_overlay(overlay);
    }
    Row("Detailed panel", "CPU threads, GPU, memory, resolution and what is limiting the frame "
                          "rate. Off shows a single line.");
    bool detail = CvarBool("port_perf_detail");
    if (ImGui::Checkbox("##detail", &detail)) SetLive("port_perf_detail", detail ? "true" : "false");
    Row("Position", nullptr);
    static const char* kCorners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    int corner = std::clamp(CvarInt("port_perf_corner"), 0, 3);
    if (ImGui::Combo("##corner", &corner, kCorners, 4)) SetLive("port_perf_corner", std::to_string(corner));
    Row("CSV log", "Writes one row of these measurements per second to the logs folder, for "
                   "comparing settings over a whole match.");
    const auto csv = host_->sampler ? host_->sampler->csv_path() : std::filesystem::path();
    if (ImGui::Button(csv.empty() ? "Start logging" : "Stop logging", ImVec2(-FLT_MIN, 0)) &&
        host_->set_csv_logging) {
      const auto file = host_->set_csv_logging(csv.empty());
      status_error_ = false;
      status_ = file.empty() ? "Logging stopped." : "Logging to " + file.string();
    }
    if (!csv.empty()) Hint(csv.string().c_str());
    ImGui::EndTable();
  }
  if (host_->profiler) {
    ImGui::Dummy(ImVec2(0, 8));
    Group("CPU PROFILER", "Where the busiest threads spend their time.", kBrass);
    const telemetry::ProfileReport report = host_->profiler->Report();
    ImGui::BeginDisabled(report.running || system.busiest_threads.empty());
    if (ImGui::Button(report.running ? "Profiling..." : "Profile the 4 busiest threads (5 s)")) {
      std::vector<std::pair<uint32_t, std::string>> threads;
      for (const auto& thread : system.busiest_threads) {
        if (threads.size() < 4 && thread.core_percent >= 5) threads.emplace_back(thread.id, thread.name);
      }
      host_->profiler->Start(std::move(threads), 5.0, host_->log_dir);
    }
    ImGui::EndDisabled();
    if (!report.summary.empty()) Hint(report.summary.c_str());
    for (const auto& thread : report.threads) {
      ImGui::TextColored(kBrass, "%s", thread.thread_name.c_str());
      for (size_t i = 0; i < thread.top.size() && i < 6; ++i) {
        ImGui::TextDisabled("%5.1f%%  %s", thread.top[i].percent, thread.top[i].location.c_str());
      }
    }
    Hint("\"guest sub_XXXXXXXX\" is a recompiled game function; a single function taking most "
         "samples on a busy thread usually means it is spinning while it waits.");
  }

  DrawCatalog("PERFORMANCE");
  ImGui::Dummy(ImVec2(0, 4));
  for (const auto& hint : DiagnosePerformance(recent, system)) {
    Hint(hint.text.c_str(), hint.warning ? &kWarning : &kOk);
  }
}

void PortMenuDialog::DrawControls() {
  const auto pads = port_input::ConnectedPads();
  Group("CONTROLLER", "Applies immediately.", kOk);
  if (pads.empty()) {
    Hint("No controller is reporting. Connect one, or press a button if it is asleep.");
  } else {
    const auto& pad = pads.front();
    ImGui::Text("%s", pad.name.empty() ? "Controller" : pad.name.c_str());
    if (pads.size() > 1) {
      ImGui::SameLine();
      ImGui::TextDisabled("+ %zu more", pads.size() - 1);
    }
    DrawStick("Left stick", pad.thumb_lx, pad.thumb_ly, CvarInt("pad_left_deadzone"));
    ImGui::SameLine(0, 30);
    DrawStick("Right stick", pad.thumb_rx, pad.thumb_ry, CvarInt("pad_right_deadzone"));
    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    ImGui::ProgressBar(pad.left_trigger / 255.0f, ImVec2(140, 0), "LT");
    ImGui::ProgressBar(pad.right_trigger / 255.0f, ImVec2(140, 0), "RT");
    ImGui::TextDisabled("Pressed: %s", PressedButtons(pad).c_str());
    ImGui::EndGroup();
  }
  if (BeginRows("pad")) {
    const auto slider = [&](const char* label, const char* hint, const char* name, int max) {
      Row(label, hint);
      int value = CvarInt(name);
      ImGui::PushID(name);
      if (ImGui::SliderInt("##v", &value, 0, max, "%d%%")) SetLive(name, std::to_string(value));
      ImGui::PopID();
    };
    slider("Left stick deadzone", "Ignore small movements from a worn stick. 0 leaves it to the game.",
           "pad_left_deadzone", 50);
    slider("Right stick deadzone", nullptr, "pad_right_deadzone", 50);
    slider("Trigger deadzone", nullptr, "pad_trigger_deadzone", 50);
    slider("Rumble strength", nullptr, "pad_vibration", 100);

    Row("Left stick also presses the D-pad", "For moves and menus that only read the D-pad.");
    bool dpad = CvarBool("pad_stick_to_dpad");
    if (ImGui::Checkbox("##dpad", &dpad)) SetLive("pad_stick_to_dpad", dpad ? "true" : "false");
    Row("Hold Back + Start for this menu", "After 0.6 s. Start closes the menu again.");
    bool chord = CvarBool("pad_menu_chord");
    if (ImGui::Checkbox("##chord", &chord)) SetLive("pad_menu_chord", chord ? "true" : "false");
    ImGui::EndTable();
  }

  ImGui::Dummy(ImVec2(0, 6));
  Group("BUTTON MAPPING", "What each button sends to the game.", kBrass);
  if (BeginRows("mapping")) {
    for (int i = 0; i < 8; ++i) {
      Row(kPadLabels[i], nullptr);
      const std::string current = Cvar(port_input::kMapCvars[i]);
      ImGui::PushID(i);
      if (ImGui::BeginCombo("##map", current.c_str())) {
        for (const char* target : port_input::kControlNames) {
          if (ImGui::Selectable(target, current == target)) SetLive(port_input::kMapCvars[i], target);
        }
        ImGui::EndCombo();
      }
      ImGui::PopID();
    }
    Row("", nullptr);
    if (ImGui::Button("Reset mapping", ImVec2(-FLT_MIN, 0))) {
      for (int i = 0; i < 8; ++i) SetLive(port_input::kMapCvars[i], port_input::kControlNames[i]);
    }
    ImGui::EndTable();
  }

  ImGui::Dummy(ImVec2(0, 6));
  Group("KEYBOARD", "Applies immediately.", kOk);
  if (BeginRows("keyboard")) {
    Row("Keyboard as controller", "Map keyboard keys to Xbox buttons.");
    bool mnk = CvarBool("mnk_mode");
    if (ImGui::Checkbox("##mnk", &mnk)) SetLive("mnk_mode", mnk ? "true" : "false");
    if (mnk) {
      for (int i = 0; i < 4; ++i) {
        Row(kKeyLabels[i], i == 0 ? "SDK key names, comma separated, e.g. Space or Semicolon,Space."
                                  : nullptr);
        ImGui::PushID(i);
        ImGui::InputText("##key", keys_[i], sizeof(keys_[i]));
        if (ImGui::IsItemDeactivatedAfterEdit()) SetLive(kKeyFlags[i], keys_[i]);
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::Dummy(ImVec2(0, 4));
  Hint("Game input is paused while this menu is open.");
}

void PortMenuDialog::DrawAbout() {
  ImGui::Dummy(ImVec2(0, 2));
  ImGui::TextColored(kBrass, "MKVDCU-Recomp for Windows PC");
  ImGui::Separator();
  ImGui::TextWrapped("F1 or Back + Start opens this menu; F1 or Start closes it and saves. "
                     "F2 shows the performance overlay. On a controller, the D-pad moves, A "
                     "selects, B backs out and the bumpers switch tabs.");
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextUnformatted("Frame rate");
  Hint("The game's simulation is tied to the 60 Hz vertical blank, so any setting that raises "
       "the frame rate also raises game speed. A real 120 FPS mode needs the game's update "
       "step decoupled from vblank, which is engine-level work not done yet.");
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextUnformatted("Settings file");
  Hint(host_->config_path.string().c_str());
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextUnformatted("Not available yet");
  Hint("Frame interpolation, output downscaling below 1x, AMD CAS/FSR (not in this SDK "
       "build), separate audio volumes.");
}

void PortMenuDialog::DrawFooter() {
  bool restart_pending = false;
  for (const auto& [name, value] : next_) {
    if (running_[name] != value) restart_pending = true;
  }
  std::string text;
  const ImVec4* color = nullptr;
  if (status_error_) {
    text = status_;
    color = &kError;
  } else if (dirty_) {
    text = "Unsaved changes. They are also saved when you close this menu.";
    color = &kBrass;
  } else if (restart_pending) {
    text = "Saved. Restart the game to use the Next launch settings.";
    color = &kWarning;
  } else if (!status_.empty()) {
    text = status_;
  } else {
    text = "All settings saved.";
  }

  ImGui::BeginDisabled(!dirty_);
  if (ImGui::Button("SAVE", ImVec2(110, 0)) && Save()) status_ = "Saved.";
  ImGui::SameLine();
  if (ImGui::Button("UNDO CHANGES", ImVec2(150, 0))) Revert();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  Hint(text.c_str(), color);
}

void PortMenuDialog::OnDraw(ImGuiIO& io) {
  FeedGamepad(io);

  const ImVec2 display = io.DisplaySize;
  auto* back = ImGui::GetBackgroundDrawList();
  back->AddRectFilled(ImVec2(0, 0), display, IM_COL32(6, 7, 9, 204));
  const float width = std::min(display.x - 32.0f, 920.0f);
  const float height = std::min(display.y - 28.0f, 680.0f);
  ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f, (display.y - height) * 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  for (const auto& c : kPalette) ImGui::PushStyleColor(c.index, c.color);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("MKVDCU-RECOMP##port_menu", nullptr, flags)) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    draw->AddRectFilled(min, ImVec2(min.x + width / 2, min.y + 4), kRed);
    draw->AddRectFilled(ImVec2(min.x + width / 2, min.y), ImVec2(min.x + width, min.y + 4), kBlue);

    const char* close_label = "CLOSE  F1";
    const float close_width =
        ImGui::CalcTextSize(close_label).x + ImGui::GetStyle().FramePadding.x * 2;
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kBrass, "MKVDCU-RECOMP");
    ImGui::SameLine();
    ImGui::TextDisabled("Port settings");
    ImGui::SameLine(right - close_width);
    if (ImGui::Button(close_label)) RequestClose();

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 2;
    if (ImGui::BeginTabBar("port_tabs")) {
      const struct {
        const char* label;
        void (PortMenuDialog::*draw)();
      } tabs[] = {{"DISPLAY", &PortMenuDialog::DrawDisplay},
                  {"GRAPHICS", &PortMenuDialog::DrawGraphics},
                  {"PERFORMANCE", &PortMenuDialog::DrawPerformance},
                  {"CONTROLS", &PortMenuDialog::DrawControls},
                  {"AUDIO", &PortMenuDialog::DrawAudio},
                  {"SYSTEM", &PortMenuDialog::DrawSystem},
                  {"ABOUT", &PortMenuDialog::DrawAbout}};
      for (int i = 0; i < int(std::size(tabs)); ++i) {
        const ImGuiTabItemFlags tab_flags = pending_tab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(tabs[i].label, nullptr, tab_flags)) {
          tab_ = i;
          ImGui::BeginChild("settings", ImVec2(0, -footer));
          (this->*tabs[i].draw)();
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      }
      pending_tab_ = -1;
      ImGui::EndTabBar();
    }
    ImGui::Separator();
    DrawFooter();
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(static_cast<int>(std::size(kPalette)));
}
