#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

class PortMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  PortMenuDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
                 std::function<void()> on_closed);
  void RequestClose() { Close(); }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  void Load();
  void Apply();
  void DrawDisplay();
  void DrawGraphics();
  void DrawPerformance();
  void DrawControls();
  void DrawInfo();

  std::filesystem::path config_path_;
  std::function<void()> on_closed_;
  int tab_ = 0;
  bool fullscreen_ = false;
  bool vsync_ = true;
  bool mnk_mode_ = false;
  int render_scale_ = 1;
  int anisotropic_ = 3;
  int effect_index_ = 0;
  std::vector<std::string> effects_;
  char key_a_[64] = {};
  char key_b_[64] = {};
  char key_x_[64] = {};
  char key_y_[64] = {};
  std::string status_;
};
