// mkvsdcu - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

class PortMenuDialog;
struct ImFont;

class MkvsdcuApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<MkvsdcuApp>(new MkvsdcuApp(ctx, "mkvsdcu",
        PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override;
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  void OnPostSetup() override;
  void OnShutdown() override;

 private:
  PortMenuDialog* port_menu_ = nullptr;
  ImFont* port_font_ = nullptr;
};
