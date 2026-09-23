#pragma once

#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "port_host.h"
#include "telemetry/frame_telemetry.h"
#include "telemetry/system_telemetry.h"

// Always-on-top performance readout, toggled with F2 or from the port menu.
class PerfOverlay final : public rex::ui::ImGuiDialog {
 public:
  PerfOverlay(rex::ui::ImGuiDrawer* drawer, const PortHost* host, std::function<void()> on_closed);
  ~PerfOverlay() override;

  void Hide() { Close(); }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  void DrawCompact(const telemetry::FrameStats& frames, const telemetry::SystemSnapshot& system);
  void DrawDetailed(ImGuiIO& io, const telemetry::FrameStats& frames,
                    const telemetry::SystemSnapshot& system);

  const PortHost* host_;
  std::function<void()> on_closed_;
  std::vector<float> frame_times_;
};

struct PerfHint {
  bool warning;
  std::string text;
};

// Plain-language reading of what is limiting performance right now.
std::vector<PerfHint> DiagnosePerformance(const telemetry::FrameStats& frames,
                                          const telemetry::SystemSnapshot& system);

// Draws the recent frame times as bars with 60 and 30 FPS guide lines.
void DrawFrameTimeGraph(const std::vector<float>& frame_times, float width, float height);
