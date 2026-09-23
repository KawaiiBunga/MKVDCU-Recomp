#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "port_host.h"
#include "port_menu/settings_catalog.h"

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
  void Undo();
  bool HasChangesSinceOpen() const;
  void SetLive(const char* name, const std::string& value);
  void SetNext(const char* name, const std::string& value);
  void Set(const settings_catalog::Setting& setting, const std::string& value);
  std::string ValueOf(const char* name) const;
  void FeedGamepad(ImGuiIO& io);

  // Rows: label on the left, control on the right; the help line at the
  // bottom describes the row under the mouse or the controller cursor.
  bool BeginRows(const char* id);
  void Row(const char* label, const char* help, bool restart = false, bool pending = false);
  void EndRows();
  void FinishRow(float bottom);

  void DrawTab(const char* tab);
  void DrawGroup(const settings_catalog::Group& group);
  void DrawSetting(const settings_catalog::Setting& setting);
  void DrawCustom(const settings_catalog::Setting& setting);
  void DrawPerformanceHeader();
  void DrawControllerHeader();
  void DrawProfileReport();
  void DrawAbout();
  void DrawFooter();

  const PortHost* host_;
  std::function<void()> on_closed_;

  // Settings only read at startup: what is running and what the next launch
  // will use (from the config file).
  std::map<std::string, std::string> running_;
  std::map<std::string, std::string> next_;
  // Every value as the menu opened, for Undo.
  std::map<std::string, std::string> open_live_;
  std::map<std::string, std::string> open_next_;

  char keys_[4][64] = {};
  std::vector<float> frame_times_;

  float row_top_ = -1;
  std::string row_help_;
  std::string help_;
  std::string frame_help_;

  int tab_ = 0;
  int pending_tab_ = -1;
  uint16_t last_pad_buttons_ = 0;
  bool dirty_ = false;
  double save_due_ = 0;
  double saved_at_ = -10;
  bool status_error_ = false;
  std::string status_;
};
