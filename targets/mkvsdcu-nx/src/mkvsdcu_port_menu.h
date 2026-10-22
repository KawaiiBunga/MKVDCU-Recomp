#pragma once
#include <functional>
#include <map>
#include <filesystem>
#include <string>
#include <rex/ui/overlay/settings_overlay.h>
#include <rex/ui/overlay/debug_overlay.h>

// Deriving from SettingsDialog lets ReXApp's existing input gate give the
// controller to this menu rather than to the game underneath it.
class MkvsdcuPortMenu final : public rex::ui::SettingsDialog {
 public:
  MkvsdcuPortMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config,
                  std::function<rex::ui::FrameStats()> stats,
                  std::function<void()> resume, std::function<void()> advanced,
                  std::map<std::string, std::string>& pending);
 protected:
  void OnDraw(ImGuiIO& io) override;
 private:
  bool Set(const char* name, const std::string& value, bool restart = false);
  void Toggle(const char* label, const char* name, bool restart = false);
  void Save();
  std::filesystem::path config_;
  std::map<std::string, std::string>& pending_;
  std::function<rex::ui::FrameStats()> stats_;
  std::function<void()> resume_, advanced_;
  std::string status_;
  bool restart_pending_ = false;
  bool focus_first_ = true;
};
