#include "port_menu.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

#include <imgui.h>
#include <rex/cvar.h>

namespace {
constexpr ImU32 kRed = IM_COL32(142, 59, 55, 255);
constexpr ImU32 kBlue = IM_COL32(55, 82, 106, 255);
constexpr ImVec4 kBrass(0.73f, 0.64f, 0.49f, 1.0f);
constexpr ImVec4 kWarning(0.80f, 0.42f, 0.34f, 1.0f);

void CopyKey(char (&out)[64], const char* name) {
  const std::string value = rex::cvar::GetFlagByName(name);
  const size_t count = std::min(value.size(), sizeof(out) - 1);
  std::memcpy(out, value.data(), count);
  out[count] = 0;
}

void Section(const char* title, const char* detail) {
  ImGui::TextColored(kBrass, "%s", title);
  ImGui::Spacing();
  ImGui::TextDisabled("%s", detail);
  ImGui::Separator();
  ImGui::Spacing();
}

void Setting(const char* title, const char* detail) {
  ImGui::TextUnformatted(title);
  ImGui::TextDisabled("%s", detail);
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

void SavePortConfig(const std::filesystem::path& path) {
  // ReXGlue's current serializer does not escape backslashes in string cvars.
  // That produces invalid TOML for Windows game, log, and user-data paths.
  std::string content = "# MKVDCU-Recomp settings\n";
  for (const auto& name : rex::cvar::ListModifiedFlags()) {
    const auto* flag = rex::cvar::GetFlagInfo(name);
    if (!flag || flag->type == rex::cvar::FlagType::Command) continue;
    const std::string value = rex::cvar::GetFlagByName(name);
    content += name + " = ";
    content += flag->type == rex::cvar::FlagType::String ? TomlString(value) : value;
    content += '\n';
  }

  std::filesystem::create_directories(path.parent_path());
  std::filesystem::path temp = path;
  temp += L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("Could not open the settings file for writing.");
    file << content;
    file.close();
    if (!file) throw std::runtime_error("Could not finish writing the settings file.");
  }
#if defined(_WIN32)
  if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::runtime_error("Could not replace the settings file (Windows error " +
                             std::to_string(GetLastError()) + ").");
  }
#else
  std::filesystem::rename(temp, path);
#endif
}
}  // namespace

PortMenuDialog::PortMenuDialog(rex::ui::ImGuiDrawer* drawer,
                               std::filesystem::path config_path,
                               std::function<void()> on_closed)
    : ImGuiDialog(drawer), config_path_(std::move(config_path)),
      on_closed_(std::move(on_closed)) {
  if (const auto* flag = rex::cvar::GetFlagInfo("present_effect")) {
    effects_ = flag->constraints.allowed_values;
  }
  if (effects_.empty()) effects_.push_back("bilinear");
  Load();
}

void PortMenuDialog::Load() {
  fullscreen_ = rex::cvar::GetFlagByName("fullscreen") == "true";
  vsync_ = rex::cvar::GetFlagByName("vsync") == "true";
  mnk_mode_ = rex::cvar::GetFlagByName("mnk_mode") == "true";
  try {
    render_scale_ = std::stoi(rex::cvar::GetFlagByName("resolution_scale"));
    anisotropic_ = std::stoi(rex::cvar::GetFlagByName("anisotropic_override"));
  } catch (...) {
    render_scale_ = 1;
    anisotropic_ = 3;
  }
  const std::string effect = rex::cvar::GetFlagByName("present_effect");
  const auto it = std::find(effects_.begin(), effects_.end(), effect);
  effect_index_ = it == effects_.end() ? 0 : static_cast<int>(it - effects_.begin());
  CopyKey(key_a_, "keybind_a");
  CopyKey(key_b_, "keybind_b");
  CopyKey(key_x_, "keybind_x");
  CopyKey(key_y_, "keybind_y");
  dirty_ = false;
}

void PortMenuDialog::Apply() {
  std::string failed;
  const auto set = [&failed](const char* name, const std::string& value) {
    if (!rex::cvar::SetFlagByName(name, value)) {
      if (!failed.empty()) failed += ", ";
      failed += name;
    }
  };
  set("fullscreen", fullscreen_ ? "true" : "false");
  set("vsync", vsync_ ? "true" : "false");
  set("resolution_scale", std::to_string(render_scale_));
  set("anisotropic_override", std::to_string(anisotropic_));
  set("present_effect", effects_[effect_index_]);
  set("mnk_mode", mnk_mode_ ? "true" : "false");
  set("keybind_a", key_a_);
  set("keybind_b", key_b_);
  set("keybind_x", key_x_);
  set("keybind_y", key_y_);
  if (!failed.empty()) {
    status_ = "Could not apply: " + failed;
    status_error_ = true;
    return;
  }
  try {
    SavePortConfig(config_path_);
    dirty_ = false;
    status_error_ = false;
    status_ = "Saved. Restart the game for resolution or filter changes.";
  } catch (const std::exception& e) {
    status_ = std::string("Could not save settings: ") + e.what();
    status_error_ = true;
  }
}

void PortMenuDialog::OnClose() {
  if (on_closed_) on_closed_();
}

void PortMenuDialog::DrawDisplay() {
  Section("01 / DISPLAY", "Display", "Window changes apply now. Internal scale needs a restart.");
  ImGui::Checkbox("Borderless fullscreen", &fullscreen_);
  ImGui::Spacing();
  ImGui::SliderInt("Internal render scale", &render_scale_, 1, 3, "%dx");
  ImGui::TextColored(ImVec4(0.75f, 0.37f, 0.31f, 1.0f), "RESTART REQUIRED  /  1x is the verified default");
  ImGui::Spacing();
  ImGui::TextDisabled("Output resolution follows the window or display. Downscaling is under investigation.");
}

void PortMenuDialog::DrawGraphics() {
  Section("02 / GRAPHICS", "Graphics", "Available settings depend on the installed renderer.");
  ImGui::SliderInt("Anisotropic filtering", &anisotropic_, -1, 5);
  ImGui::TextDisabled("-1 game default  |  3 = 4x  |  5 = 16x");
  ImGui::Spacing();
  if (ImGui::BeginCombo("Output filter", effects_[effect_index_].c_str())) {
    for (int i = 0; i < static_cast<int>(effects_.size()); ++i) {
      if (ImGui::Selectable(effects_[i].c_str(), effect_index_ == i)) effect_index_ = i;
    }
    ImGui::EndCombo();
  }
  ImGui::TextColored(ImVec4(0.75f, 0.37f, 0.31f, 1.0f), "RESTART REQUIRED  /  temporal filters remain experimental");
}

void PortMenuDialog::DrawPerformance() {
  Section("03 / PERFORMANCE", "Performance", "Frame rate options need gameplay timing tests.");
  ImGui::Checkbox("Vertical sync", &vsync_);
  ImGui::Spacing();
  ImGui::TextUnformatted("FRAME RATE  /  VERIFIED BASELINE");
  ImGui::TextDisabled("The guest refresh setting is not a safe FPS unlock. 30/60/120 presets will appear after full-match timing tests.");
}

void PortMenuDialog::DrawControls() {
  Section("04 / CONTROLS", "Controls", "Keyboard mappings emulate Xbox buttons. F1 always opens this menu.");
  ImGui::Checkbox("Enable keyboard controller", &mnk_mode_);
  if (mnk_mode_) {
    ImGui::InputText("A button", key_a_, sizeof(key_a_));
    ImGui::InputText("B button", key_b_, sizeof(key_b_));
    ImGui::InputText("X button", key_x_, sizeof(key_x_));
    ImGui::InputText("Y button", key_y_, sizeof(key_y_));
    ImGui::TextDisabled("Use SDK key names, such as Space, Semicolon, L, or P.");
  }
  ImGui::Spacing();
  ImGui::TextDisabled("Controller button profiles and menu chord need a host input remap layer.");
}

void PortMenuDialog::DrawInfo() {
  Section("05 / SYSTEM", "About this port", "MKVDCU-Recomp for Windows PC");
  ImGui::TextWrapped("Press F1 to open or close port settings. Use Apply & Save to keep changes after quitting.");
  ImGui::Spacing();
  ImGui::TextDisabled("The match may continue while this overlay is open. Guest controls are neutralized until it closes.");
}

void PortMenuDialog::OnDraw(ImGuiIO& io) {
  const ImVec2 display = io.DisplaySize;
  auto* back = ImGui::GetBackgroundDrawList();
  back->AddRectFilled(ImVec2(0, 0), display, IM_COL32(6, 7, 9, 204));
  back->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(display.x, display.y),
                                IM_COL32(68, 23, 25, 82), IM_COL32(30, 43, 54, 82),
                                IM_COL32(30, 43, 54, 82), IM_COL32(68, 23, 25, 82));
  const float width = std::min(display.x - 32.0f, 980.0f);
  const float height = std::min(display.y - 28.0f, 630.0f);
  ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f, (display.y - height) * 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.12f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.39f, 0.37f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.12f, 0.13f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.14f, 0.15f, 0.16f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.16f, 0.17f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.25f, 0.26f, 0.27f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.31f, 0.25f, 0.24f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.24f, 0.23f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.48f, 0.27f, 0.24f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58f, 0.30f, 0.26f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.32f, 0.37f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.39f, 0.41f, 0.42f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.45f, 0.25f, 0.23f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.73f, 0.64f, 0.49f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.67f, 0.47f, 0.32f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.78f, 0.56f, 0.38f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.37f, 0.36f, 0.34f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("MKVDCU-RECOMP##port_menu", nullptr, flags)) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    draw->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + width / 2, min.y + 5), kRed);
    draw->AddRectFilled(ImVec2(min.x + width / 2, min.y), ImVec2(min.x + width, min.y + 5), kBlue);
    ImGui::Dummy(ImVec2(0, 13));
    ImGui::TextColored(ImVec4(0.73f, 0.64f, 0.49f, 1.0f), "MKVDCU-RECOMP  /  PC PORT");
    ImGui::SameLine(width - 91);
    if (ImGui::Button("CLOSE  F1")) RequestClose();
    ImGui::TextUnformatted("PORT MENU");
    ImGui::TextDisabled("DISPLAY  /  GRAPHICS  /  PERFORMANCE  /  CONTROLS");
    ImGui::Separator();
    const char* tabs[] = {"DISPLAY", "GRAPHICS", "PERFORMANCE", "CONTROLS", "SYSTEM"};
    for (int i = 0; i < 5; ++i) {
      if (i) ImGui::SameLine();
      if (ImGui::Selectable(tabs[i], tab_ == i, 0, ImVec2((width - 45) / 5, 34))) tab_ = i;
    }
    ImGui::Separator();
    ImGui::BeginChild("settings", ImVec2(0, -67), false);
    switch (tab_) {
      case 0: DrawDisplay(); break;
      case 1: DrawGraphics(); break;
      case 2: DrawPerformance(); break;
      case 3: DrawControls(); break;
      default: DrawInfo(); break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("APPLY & SAVE", ImVec2(155, 34))) Apply();
    ImGui::SameLine();
    if (ImGui::Button("RELOAD", ImVec2(92, 34))) { Load(); status_ = "Loaded saved settings."; }
    ImGui::SameLine();
    ImGui::TextColored(status_.empty() ? ImVec4(0.65f, 0.65f, 0.63f, 1) :
                                      ImVec4(0.75f, 0.55f, 0.37f, 1), "%s",
                       status_.empty() ? "Some settings take effect after restarting the game." : status_.c_str());
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(17);
}
