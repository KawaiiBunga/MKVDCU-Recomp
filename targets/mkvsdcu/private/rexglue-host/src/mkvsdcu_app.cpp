#include "generated/default/mkvsdcu_init.h"
#include "mkvsdcu_app.h"

#include <imgui.h>
#include <filesystem>
#include <rex/input/input_system.h>
#include <rex/ui/keybinds.h>

#include "port_menu/port_menu.h"

void MkvsdcuApp::OnConfigurePaths(rex::PathConfig& paths) {
  paths.config_path = paths.user_data_root / "mkvsdcu.toml";
}

void MkvsdcuApp::OnConfigureFonts(ImFontAtlas* atlas) {
  const char* font = "C:\\Windows\\Fonts\\bahnschrift.ttf";
  if (std::filesystem::exists(font)) port_font_ = atlas->AddFontFromFileTTF(font, 18.0f);
}

void MkvsdcuApp::OnConfigureStyle(ImGuiStyle& style, rex::ui::Style&) {
  ImGui::StyleColorsDark(&style);
  if (port_font_) ImGui::GetIO().FontDefault = port_font_;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  style.WindowRounding = 2.0f;
  style.FrameRounding = 2.0f;
  style.WindowPadding = ImVec2(24, 18);
  style.FramePadding = ImVec2(10, 8);
  style.ItemSpacing = ImVec2(12, 12);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.10f, 0.14f, 0.98f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.19f, 0.24f, 1.0f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.31f, 0.36f, 1.0f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.21f, 0.52f, 0.57f, 1.0f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.36f, 0.85f, 0.90f, 1.0f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.65f, 0.70f, 1.0f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.17f, 0.31f, 0.37f, 1.0f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.51f, 0.58f, 1.0f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.37f, 0.86f, 0.91f, 1.0f);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.93f, 0.42f, 0.34f, 1.0f);
}

void MkvsdcuApp::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  rex::ui::RegisterBind("bind_port_menu", "F1", "Toggle MKVDCU Port Menu", [this, drawer]() {
    if (port_menu_) {
      port_menu_->RequestClose();
    } else {
      port_menu_ = new PortMenuDialog(drawer, user_data_root() / "mkvsdcu.toml",
                                      [this]() { port_menu_ = nullptr; });
    }
  });
}

void MkvsdcuApp::OnPostSetup() {
  auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
  if (input) {
    input->SetActiveCallback([this]() {
      return port_menu_ == nullptr && !imgui_drawer()->GetIO().WantCaptureMouse;
    });
  }
}

void MkvsdcuApp::OnShutdown() {
  rex::ui::UnregisterBind("bind_port_menu");
  delete port_menu_;
  port_menu_ = nullptr;
}
