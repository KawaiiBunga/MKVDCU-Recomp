#include "port_menu.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include <imgui.h>
#include <rex/cvar.h>

namespace {
constexpr ImU32 kEmber = IM_COL32(238, 108, 88, 255);
constexpr ImU32 kCyan = IM_COL32(92, 216, 228, 255);

void CopyKey(char (&out)[64], const char* name) {
  const std::string value = rex::cvar::GetFlagByName(name);
  const size_t count = std::min(value.size(), sizeof(out) - 1);
  std::memcpy(out, value.data(), count);
  out[count] = 0;
}

void Section(const char* overline, const char* title, const char* detail) {
  ImGui::TextColored(ImVec4(0.36f, 0.85f, 0.90f, 1.0f), "%s", overline);
  ImGui::Spacing();
  ImGui::TextUnformatted(title);
  ImGui::Separator();
  ImGui::TextDisabled("%s", detail);
  ImGui::Spacing();
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
}

void PortMenuDialog::Apply() {
  // The SDK's lifecycle declarations determine which changes take effect now.
  const bool ok =
      rex::cvar::SetFlagByName("fullscreen", fullscreen_ ? "true" : "false") &&
      rex::cvar::SetFlagByName("vsync", vsync_ ? "true" : "false") &&
      rex::cvar::SetFlagByName("resolution_scale", std::to_string(render_scale_)) &&
      rex::cvar::SetFlagByName("anisotropic_override", std::to_string(anisotropic_)) &&
      rex::cvar::SetFlagByName("present_effect", effects_[effect_index_]) &&
      rex::cvar::SetFlagByName("mnk_mode", mnk_mode_ ? "true" : "false") &&
      rex::cvar::SetFlagByName("keybind_a", key_a_) &&
      rex::cvar::SetFlagByName("keybind_b", key_b_) &&
      rex::cvar::SetFlagByName("keybind_x", key_x_) &&
      rex::cvar::SetFlagByName("keybind_y", key_y_);
  if (!ok) {
    status_ = "A value was rejected. Check ranges and key names.";
    return;
  }
  try {
    std::filesystem::create_directories(config_path_.parent_path());
    rex::cvar::SaveConfig(config_path_);
    status_ = "Saved. Render scale and output filter apply after restart.";
  } catch (const std::exception& e) {
    status_ = std::string("Could not save settings: ") + e.what();
  }
}

void PortMenuDialog::OnClose() {
  if (on_closed_) on_closed_();
}

void PortMenuDialog::DrawDisplay() {
  Section("01 / DISPLAY", "The frame around the fight", "Window changes apply live. Internal scale needs a restart.");
  ImGui::Checkbox("Borderless fullscreen", &fullscreen_);
  ImGui::Spacing();
  ImGui::SliderInt("Internal render scale", &render_scale_, 1, 3, "%dx");
  ImGui::TextColored(ImVec4(0.93f, 0.55f, 0.40f, 1.0f), "RESTART REQUIRED  /  1x is the verified default");
  ImGui::Spacing();
  ImGui::TextDisabled("Output resolution follows the window or display. Downscaling is under investigation.");
}

void PortMenuDialog::DrawGraphics() {
  Section("02 / GRAPHICS", "Shape the image", "Changes are only exposed when the installed renderer supports them.");
  ImGui::SliderInt("Anisotropic filtering", &anisotropic_, -1, 5);
  ImGui::TextDisabled("-1 game default  |  3 = 4x  |  5 = 16x");
  ImGui::Spacing();
  if (ImGui::BeginCombo("Output filter", effects_[effect_index_].c_str())) {
    for (int i = 0; i < static_cast<int>(effects_.size()); ++i) {
      if (ImGui::Selectable(effects_[i].c_str(), effect_index_ == i)) effect_index_ = i;
    }
    ImGui::EndCombo();
  }
  ImGui::TextColored(ImVec4(0.93f, 0.55f, 0.40f, 1.0f), "RESTART REQUIRED  /  temporal filters remain experimental");
}

void PortMenuDialog::DrawPerformance() {
  Section("03 / PERFORMANCE", "Keep the match in sync", "Presentation controls must not change combat timing.");
  ImGui::Checkbox("Vertical sync", &vsync_);
  ImGui::Spacing();
  ImGui::TextUnformatted("FRAME RATE  /  VERIFIED BASELINE");
  ImGui::TextDisabled("The guest refresh setting is not a safe FPS unlock. 30/60/120 presets will appear after full-match timing tests.");
}

void PortMenuDialog::DrawControls() {
  Section("04 / CONTROLS", "Command the fight", "Keyboard mappings emulate Xbox buttons. F1 remains the recovery key.");
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
  Section("05 / SYSTEM", "MKVDCU-Recomp", "PC host  /  ReXGlue runtime  /  exact retail base XEX");
  ImGui::TextWrapped("This menu belongs to the PC port. Save your settings here; F1 opens and closes it during play.");
  ImGui::Spacing();
  ImGui::TextDisabled("The match may continue while this overlay is open. Guest controls are neutralized until it closes.");
}

void PortMenuDialog::OnDraw(ImGuiIO& io) {
  const ImVec2 display = io.DisplaySize;
  auto* back = ImGui::GetBackgroundDrawList();
  back->AddRectFilled(ImVec2(0, 0), display, IM_COL32(4, 8, 14, 194));
  back->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(display.x, display.y),
                                IM_COL32(56, 22, 27, 90), IM_COL32(14, 51, 63, 90),
                                IM_COL32(14, 51, 63, 90), IM_COL32(56, 22, 27, 90));
  const float width = std::min(display.x - 32.0f, 980.0f);
  const float height = std::min(display.y - 28.0f, 630.0f);
  ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f, (display.y - height) * 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.10f, 0.14f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.31f, 0.48f, 0.56f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("THE GATE##port_menu", nullptr, flags)) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    draw->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + width / 2, min.y + 5), kEmber);
    draw->AddRectFilled(ImVec2(min.x + width / 2, min.y), ImVec2(min.x + width, min.y + 5), kCyan);
    ImGui::Dummy(ImVec2(0, 13));
    ImGui::TextColored(ImVec4(0.37f, 0.85f, 0.89f, 1.0f), "MKVDCU-RECOMP  //  THE GATE");
    ImGui::SameLine(width - 91);
    if (ImGui::Button("CLOSE  F1")) RequestClose();
    ImGui::TextUnformatted("PORT MENU");
    ImGui::TextDisabled("THE FIGHT IS YOURS TO CONFIGURE");
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
    ImGui::TextColored(status_.empty() ? ImVec4(0.59f, 0.67f, 0.72f, 1) :
                                      ImVec4(0.93f, 0.62f, 0.48f, 1), "%s",
                       status_.empty() ? "LIVE / RESTART LABELS SHOWN PER SETTING" : status_.c_str());
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}
