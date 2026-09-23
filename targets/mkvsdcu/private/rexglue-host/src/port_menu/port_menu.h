#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "port_host.h"

class PortMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  PortMenuDialog(rex::ui::ImGuiDrawer* drawer, const PortHost* host, std::function<void()> on_closed);
  ~PortMenuDialog() override;

  // Saves unsaved changes, then closes. Stays open if saving fails.
  void RequestClose();
  bool SaveIfDirty() { return !dirty_ || Save(); }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  void Load();
  bool Save();
  void Revert();
  void SetLive(const char* name, const std::string& value);
  void SetNext(const char* name, const std::string& value);
  void FeedGamepad(ImGuiIO& io);

  void DrawDisplay();
  void DrawGraphics();
  void DrawPerformance();
  void DrawControls();
  void DrawAudio();
  void DrawSystem();
  void DrawCatalog(const char* tab);
  void DrawAbout();
  void DrawFooter();

  const PortHost* host_;
  std::function<void()> on_closed_;

  // Live settings are read straight from their cvars; these are the values
  // last saved, for Undo.
  std::vector<std::pair<std::string, std::string>> saved_live_;
  // Settings only read at startup: what is running and what the next launch
  // will use (from the config file).
  std::map<std::string, std::string> running_;
  std::map<std::string, std::string> next_;

  char keys_[4][64] = {};
  std::vector<float> frame_times_;
  int monitor_count_ = 1;

  int tab_ = 0;
  int pending_tab_ = -1;
  uint16_t last_pad_buttons_ = 0;
  bool dirty_ = false;
  bool status_error_ = false;
  std::string status_;
};
