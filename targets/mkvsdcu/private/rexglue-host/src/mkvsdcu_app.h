// mkvsdcu - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include <rex/rex_app.h>

#include "port_host.h"
#include "telemetry/guest_profiler.h"
#include "telemetry/system_telemetry.h"

class PerfOverlay;
class PortMenuDialog;
struct ImFont;

class MkvsdcuApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  ~MkvsdcuApp() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<MkvsdcuApp>(new MkvsdcuApp(ctx, "mkvsdcu",
        PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override;
  void OnPreSetup(rex::RuntimeConfig& config) override;
  void OnPostLoadXexImage() override;
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  void OnPostSetup() override;
  void OnShutdown() override;

 private:
  void OpenPortMenu();
  void ShowPerfOverlay(bool visible);
  PortWindowInfo WindowInfo() const;
  void ResizeWindow(uint32_t width, uint32_t height);

  PortHost host_;
  std::unique_ptr<telemetry::SystemSampler> sampler_;
  telemetry::GuestProfiler profiler_;
  std::thread auto_profile_;
  std::atomic<bool> stopping_{false};
  PortMenuDialog* port_menu_ = nullptr;
  PerfOverlay* perf_overlay_ = nullptr;
  ImFont* port_font_ = nullptr;
};
