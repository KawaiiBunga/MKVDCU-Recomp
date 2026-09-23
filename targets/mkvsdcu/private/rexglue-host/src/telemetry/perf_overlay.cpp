#include "perf_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(port_perf_overlay, false, "Port/Overlay", "Show the performance overlay");
REXCVAR_DEFINE_BOOL(port_perf_detail, false, "Port/Overlay",
                    "Show the detailed performance panel instead of the compact bar");
REXCVAR_DEFINE_INT32(port_perf_corner, 0, "Port/Overlay",
                     "Overlay corner: 0 top left, 1 top right, 2 bottom left, 3 bottom right")
    .range(0, 3);
REXCVAR_DEFINE_BOOL(port_perf_csv, false, "Port/Overlay",
                    "Start the per-second performance CSV log at launch (for benchmarks)");
REXCVAR_DEFINE_INT32(port_profile_after, 0, "Port/Overlay",
                     "Profile the busiest threads this many seconds after launch (0 = off)")
    .range(0, 3600);

namespace {
constexpr ImVec4 kGood(0.55f, 0.80f, 0.56f, 1.0f);
constexpr ImVec4 kWarn(0.93f, 0.66f, 0.35f, 1.0f);
constexpr ImVec4 kBad(0.93f, 0.40f, 0.36f, 1.0f);
constexpr ImVec4 kLabel(0.62f, 0.62f, 0.60f, 1.0f);
constexpr ImVec4 kBrass(0.73f, 0.64f, 0.49f, 1.0f);

double TargetFps(const telemetry::SystemSnapshot& system) {
  (void)system;
  // The game paces itself on the guest vblank, which runs at this rate unless
  // Game timing is unlocked.
  const double rate = rex::cvar::Query<double>("video_mode_refresh_rate");
  return rate > 1 ? rate : 60.0;
}

ImVec4 FpsColor(double fps, double target) {
  return fps >= target * 0.97 ? kGood : fps >= target * 0.8 ? kWarn : kBad;
}

ImVec4 LoadColor(double percent) {
  return percent < 75 ? kGood : percent < 92 ? kWarn : kBad;
}

std::string Bytes(uint64_t bytes) {
  char text[32];
  if (bytes >= (1ull << 30)) {
    std::snprintf(text, sizeof(text), "%.2f GB", bytes / double(1ull << 30));
  } else {
    std::snprintf(text, sizeof(text), "%.0f MB", bytes / double(1ull << 20));
  }
  return text;
}

std::string Percent(double value) {
  if (value < 0) return "n/a";
  char text[16];
  std::snprintf(text, sizeof(text), "%.0f%%", value);
  return text;
}

void Label(const char* text) {
  ImGui::TextColored(kLabel, "%s", text);
  ImGui::SameLine(150);
}

void Heading(const char* text) {
  ImGui::Dummy(ImVec2(0, 2));
  ImGui::TextColored(kBrass, "%s", text);
  ImGui::Separator();
}

int ScaleSetting() {
  return std::max(1, rex::cvar::Query<int32_t>("resolution_scale"));
}
}  // namespace

std::vector<PerfHint> DiagnosePerformance(const telemetry::FrameStats& frames,
                                          const telemetry::SystemSnapshot& system) {
  std::vector<PerfHint> hints;
  char text[256];
  if (frames.samples == 0 || frames.seconds_since_last_frame > 1.0) {
    hints.push_back({true, "The game is not presenting frames right now (loading or stalled)."});
    return hints;
  }
  const double target = TargetFps(system);
  const telemetry::ThreadLoad* busiest =
      system.busiest_threads.empty() ? nullptr : &system.busiest_threads.front();

  if (frames.fps < target * 0.95) {
    if (busiest && busiest->core_percent >= 90) {
      std::snprintf(text, sizeof(text),
                    "Likely CPU-bound: thread '%s' is using %.0f%% of one core. Some of that can "
                    "be spin-waiting; F1 > Performance > CPU profiler shows where it goes.",
                    busiest->name.c_str(), busiest->core_percent);
      hints.push_back({true, text});
    } else if (system.gpu_3d_percent >= 90) {
      std::snprintf(text, sizeof(text),
                    "GPU-bound: the GPU is %.0f%% busy with this game. Lower the internal render "
                    "scale.",
                    system.gpu_3d_percent);
      hints.push_back({true, text});
    } else {
      std::snprintf(text, sizeof(text),
                    "Below %.0f FPS with CPU and GPU headroom: the game is waiting on "
                    "synchronisation, shader compiles or streaming.",
                    target);
      hints.push_back({true, text});
    }
  }
  if (frames.hitches > 0) {
    std::snprintf(text, sizeof(text),
                  "%u hitch%s in the last 5 s (worst %.1f ms). Usually shader compilation or "
                  "streaming; shader hitches fade as the cache fills.",
                  frames.hitches, frames.hitches == 1 ? "" : "es", frames.max_ms);
    hints.push_back({true, text});
  } else if (frames.p99_ms > frames.avg_ms * 1.25) {
    std::snprintf(text, sizeof(text),
                  "Uneven pacing: 1%% of frames take %.1f ms against a %.1f ms average.",
                  frames.p99_ms, frames.avg_ms);
    hints.push_back({true, text});
  }
  if (system.vram_budget_bytes && system.vram_used_bytes > system.vram_budget_bytes * 0.9) {
    hints.push_back({true, "Video memory is over 90% of the budget Windows allows this game. "
                           "Expect stutter; lower the render scale."});
  }
  if (system.system_cpu_percent > 90) {
    std::snprintf(text, sizeof(text),
                  "The whole CPU is %.0f%% busy. Close background programs.",
                  system.system_cpu_percent);
    hints.push_back({true, text});
  }
  if (system.gpu_3d_total_percent >= 0 && system.gpu_3d_percent >= 0 &&
      system.gpu_3d_total_percent - system.gpu_3d_percent > 30) {
    std::snprintf(text, sizeof(text), "Other programs are using %.0f%% of the GPU.",
                  system.gpu_3d_total_percent - system.gpu_3d_percent);
    hints.push_back({true, text});
  }
  if (hints.empty()) {
    std::snprintf(text, sizeof(text), "Holding %.0f FPS with steady pacing.", frames.fps);
    hints.push_back({false, text});
    if (system.gpu_3d_percent >= 0 && system.gpu_3d_percent < 45 && ScaleSetting() < 3) {
      std::snprintf(text, sizeof(text),
                    "The GPU is only %.0f%% busy; a higher render scale is likely affordable.",
                    system.gpu_3d_percent);
      hints.push_back({false, text});
    }
  }
  return hints;
}

void DrawFrameTimeGraph(const std::vector<float>& frame_times, float width, float height) {
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(width, height));
  auto* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      IM_COL32(20, 22, 24, 200));
  float top = 40.0f;
  for (float v : frame_times) top = std::max(top, v * 1.1f);
  const auto y_of = [&](float ms) { return origin.y + height - std::min(ms / top, 1.0f) * height; };
  for (const float guide : {1000.0f / 60.0f, 1000.0f / 30.0f}) {
    const float y = y_of(guide);
    draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), IM_COL32(120, 120, 115, 110));
  }
  if (frame_times.empty()) return;
  const float step = width / float(frame_times.size());
  for (size_t i = 0; i < frame_times.size(); ++i) {
    const float v = frame_times[i];
    const ImU32 color = v <= 17.5f ? IM_COL32(120, 190, 125, 255)
                        : v <= 34.0f ? IM_COL32(230, 165, 85, 255)
                                     : IM_COL32(230, 95, 85, 255);
    const float x = origin.x + i * step;
    draw->AddRectFilled(ImVec2(x, y_of(v)), ImVec2(x + std::max(step - 0.5f, 1.0f), origin.y + height),
                        color);
  }
}

PerfOverlay::PerfOverlay(rex::ui::ImGuiDrawer* drawer, const PortHost* host,
                         std::function<void()> on_closed)
    : ImGuiDialog(drawer), host_(host), on_closed_(std::move(on_closed)) {
  if (host_->sampler) host_->sampler->SetWanted("overlay", true);
}

PerfOverlay::~PerfOverlay() {
  if (host_->sampler) host_->sampler->SetWanted("overlay", false);
}

void PerfOverlay::OnClose() {
  if (on_closed_) on_closed_();
}

void PerfOverlay::OnDraw(ImGuiIO& io) {
  const telemetry::FrameStats frames = telemetry::ComputeFrameStats(5.0);
  const telemetry::SystemSnapshot system =
      host_->sampler ? host_->sampler->Latest() : telemetry::SystemSnapshot{};

  const int corner = std::clamp(REXCVAR_GET(port_perf_corner), 0, 3);
  const float pad = 12.0f;
  const ImVec2 pos((corner & 1) ? io.DisplaySize.x - pad : pad,
                   (corner & 2) ? io.DisplaySize.y - pad : pad);
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always,
                          ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f));
  ImGui::SetNextWindowBgAlpha(0.80f);
  const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
  ImGui::PushFont(nullptr, 15.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 9));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
  if (ImGui::Begin("##perf_overlay", nullptr, flags)) {
    if (REXCVAR_GET(port_perf_detail)) {
      DrawDetailed(io, frames, system);
    } else {
      DrawCompact(frames, system);
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopFont();
}

void PerfOverlay::DrawCompact(const telemetry::FrameStats& frames,
                              const telemetry::SystemSnapshot& system) {
  const double target = TargetFps(system);
  const telemetry::FrameStats now = telemetry::ComputeFrameStats(1.0);
  ImGui::TextColored(FpsColor(now.fps, target), "%.0f FPS", now.fps);
  ImGui::SameLine();
  ImGui::Text("%.1f ms", now.avg_ms);
  ImGui::SameLine();
  ImGui::TextColored(kLabel, "1%% %.1f ms", frames.p99_ms);
  if (system.valid) {
    ImGui::SameLine();
    ImGui::TextColored(kLabel, "CPU");
    ImGui::SameLine(0, 4);
    ImGui::TextColored(LoadColor(system.process_cpu_percent), "%.0f%%", system.process_cpu_percent);
    ImGui::SameLine();
    ImGui::TextColored(kLabel, "GPU");
    ImGui::SameLine(0, 4);
    ImGui::TextColored(system.gpu_3d_percent < 0 ? kLabel : LoadColor(system.gpu_3d_percent), "%s",
                       Percent(system.gpu_3d_percent).c_str());
  }
  const telemetry::GuestOutputSize size = telemetry::LastGuestOutputSize();
  if (size.width) {
    ImGui::SameLine();
    ImGui::TextColored(kLabel, "%ux%u %dx", size.width, size.height, ScaleSetting());
  }
  telemetry::RecentFrameTimes(frame_times_, 180);
  DrawFrameTimeGraph(frame_times_, ImGui::GetContentRegionAvail().x > 200
                                       ? ImGui::GetContentRegionAvail().x
                                       : 360.0f,
                     28.0f);
}

void PerfOverlay::DrawDetailed(ImGuiIO& io, const telemetry::FrameStats& frames,
                               const telemetry::SystemSnapshot& system) {
  const double target = TargetFps(system);
  const telemetry::FrameStats now = telemetry::ComputeFrameStats(1.0);
  ImGui::TextColored(kBrass, "MKVDCU-RECOMP");
  ImGui::SameLine();
  ImGui::TextColored(kLabel, "performance   F2 hides");

  Heading("GAME FRAMES");
  Label("Frame rate");
  ImGui::TextColored(FpsColor(now.fps, target), "%.1f FPS", now.fps);
  ImGui::SameLine();
  ImGui::TextColored(kLabel, "(target %.0f)", target);
  Label("Frame time");
  ImGui::Text("avg %.2f   min %.2f   max %.2f ms", frames.avg_ms, frames.min_ms, frames.max_ms);
  Label("Lows");
  ImGui::Text("1%% %.2f ms (%.0f FPS)   0.1%% %.2f ms", frames.p99_ms,
              frames.p99_ms > 0 ? 1000.0 / frames.p99_ms : 0.0, frames.p999_ms);
  Label("Pacing");
  ImGui::TextColored(frames.stddev_ms < 1.5 ? kGood : frames.stddev_ms < 4 ? kWarn : kBad,
                     "jitter %.2f ms", frames.stddev_ms);
  ImGui::SameLine();
  ImGui::TextColored(frames.hitches ? kWarn : kLabel, "hitches (5 s) %u", frames.hitches);
  Label("Guest vblank");
  ImGui::Text("%.1f Hz", system.vblank_hz);
  ImGui::SameLine();
  ImGui::TextColored(kLabel, "window repaint %.0f Hz", io.Framerate);
  telemetry::RecentFrameTimes(frame_times_, 240);
  DrawFrameTimeGraph(frame_times_, 400.0f, 54.0f);
  ImGui::TextColored(kLabel, "last 240 frames   lines: 16.7 ms (60 FPS), 33.3 ms (30 FPS)");

  Heading("RESOLUTION");
  const telemetry::GuestOutputSize size = telemetry::LastGuestOutputSize();
  const int scale = ScaleSetting();
  Label("Game output");
  if (size.width) {
    ImGui::Text("%u x %u", size.width, size.height);
  } else {
    ImGui::TextColored(kLabel, "waiting for the first frame");
  }
  Label("Internal render");
  ImGui::Text("%u x %u  (%dx scale)", size.width * scale, size.height * scale, scale);
  if (host_->window_info) {
    const PortWindowInfo window = host_->window_info();
    Label("Window");
    ImGui::Text("%u x %u  %s", window.width, window.height,
                window.fullscreen ? "borderless fullscreen" : "windowed");
    if (size.height && window.height) {
      Label("Presented at");
      ImGui::Text("%.2fx the internal height", double(window.height) / (size.height * scale));
    }
  }

  Heading("CPU");
  if (!system.valid) {
    ImGui::TextColored(kLabel, "collecting...");
  } else {
    Label("This game");
    ImGui::TextColored(LoadColor(system.process_cpu_percent), "%.0f%% of %u threads", system.process_cpu_percent,
                       system.logical_cores);
    ImGui::SameLine();
    ImGui::TextColored(kLabel, "(= %.1f cores)", system.process_cpu_percent * system.logical_cores / 100.0);
    Label("Whole system");
    ImGui::Text("%.0f%%   %u threads in game", system.system_cpu_percent, system.thread_count);
    ImGui::TextColored(kLabel, "Busiest threads (share of one core)");
    for (const auto& thread : system.busiest_threads) {
      if (thread.core_percent < 1.0) break;
      const float fraction = float(std::min(thread.core_percent, 100.0) / 100.0);
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, LoadColor(thread.core_percent));
      char overlay[96];
      std::snprintf(overlay, sizeof(overlay), "%.0f%%  %s", thread.core_percent, thread.name.c_str());
      ImGui::ProgressBar(fraction, ImVec2(400.0f, 0), overlay);
      ImGui::PopStyleColor();
    }
  }

  Heading("GPU AND MEMORY");
  if (system.valid) {
    Label("Adapter");
    ImGui::Text("%s", system.gpu_name.empty() ? "unknown" : system.gpu_name.c_str());
    Label("GPU busy");
    ImGui::TextColored(system.gpu_3d_percent < 0 ? kLabel : LoadColor(system.gpu_3d_percent), "%s",
                       Percent(system.gpu_3d_percent).c_str());
    ImGui::SameLine();
    ImGui::TextColored(kLabel, "%s engine, all programs %s",
                       system.gpu_engine.empty() ? "busiest" : system.gpu_engine.c_str(),
                       Percent(system.gpu_3d_total_percent).c_str());
    Label("Video memory");
    const double vram_share =
        system.vram_budget_bytes ? 100.0 * system.vram_used_bytes / system.vram_budget_bytes : 0;
    ImGui::TextColored(LoadColor(vram_share), "%s of %s budget", Bytes(system.vram_used_bytes).c_str(),
                       Bytes(system.vram_budget_bytes).c_str());
    ImGui::SameLine();
    ImGui::TextColored(kLabel, "shared %s", Bytes(system.shared_used_bytes).c_str());
    Label("RAM");
    ImGui::Text("%s working set, %s committed", Bytes(system.working_set_bytes).c_str(),
                Bytes(system.private_bytes).c_str());
    Label("Page faults");
    ImGui::Text("%.0f / s", system.page_faults_per_second);
  }

  Heading("WHAT IS LIMITING IT");
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0f);
  for (const auto& hint : DiagnosePerformance(frames, system)) {
    ImGui::TextColored(hint.warning ? kWarn : kGood, "%s", hint.text.c_str());
  }
  ImGui::PopTextWrapPos();
  const auto csv = host_->sampler ? host_->sampler->csv_path() : std::filesystem::path();
  if (!csv.empty()) {
    ImGui::TextColored(kLabel, "Logging to %s", csv.filename().string().c_str());
  }
}
