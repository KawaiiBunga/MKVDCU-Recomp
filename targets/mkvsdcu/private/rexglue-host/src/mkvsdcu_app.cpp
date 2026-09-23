#include "generated/default/mkvsdcu_init.h"
#include "mkvsdcu_app.h"

#include <imgui.h>
#include <algorithm>
#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/input/input_system.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>

#include "host_tweaks.h"
#include "input/controller_filter.h"
#include "port_menu/port_config.h"
#include "port_menu/port_menu.h"
#include "telemetry/perf_overlay.h"
#include "telemetry/system_telemetry.h"

MkvsdcuApp::~MkvsdcuApp() = default;

void MkvsdcuApp::OnConfigurePaths(rex::PathConfig& paths) {
  paths.config_path = paths.user_data_root / "mkvsdcu.toml";
  RepairPortConfig(paths.config_path);
  host_.config_path = paths.config_path;
  host_.log_dir = paths.user_data_root / "logs";
}

// Developer aid: MKVDCU_DUMP_IMAGE=<file> writes the loaded XEX image so
// indirect-call targets can be found offline (scripts/find-function-seeds.py).
void MkvsdcuApp::OnPostLoadXexImage() {
  wchar_t path[MAX_PATH];
  if (!GetEnvironmentVariableW(L"MKVDCU_DUMP_IMAGE", path, MAX_PATH)) return;
  constexpr uint32_t kImageBase = 0x82000000, kImageSize = 0x01130000;
  const uint8_t* image = runtime()->memory()->virtual_membase() + kImageBase;
  if (FILE* file = _wfopen(path, L"wb")) {
    fwrite(image, 1, kImageSize, file);
    fclose(file);
  }
}

void MkvsdcuApp::OnPreSetup(rex::RuntimeConfig& config) {
  // Load the GPU plugin ourselves to pick its backend. If that backend is not
  // compiled in, ReXApp falls back to loading the plugin's default.
  const std::string plugin = rex::cvar::GetFlagByName("gpu_plugin");
  if (!config.graphics && !plugin.empty()) {
    config.graphics = rex::system::LoadGpuPlugin(plugin, host_tweaks::GpuBackend());
  }

  config.input_factory = [this](bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
    return port_input::CreateInputSystem(tool_mode, [this] {
      app_context().CallInUIThreadDeferred([this] { OpenPortMenu(); });
    });
  };
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
  host_.window_info = [this] { return WindowInfo(); };
  host_.resize_window = [this](uint32_t w, uint32_t h) { ResizeWindow(w, h); };
  host_.show_perf_overlay = [this](bool visible) { ShowPerfOverlay(visible); };
  host_.set_csv_logging = [this](bool enabled) {
    return sampler_ ? sampler_->SetCsvLogging(enabled, host_.log_dir) : std::filesystem::path();
  };

  rex::ui::RegisterBind("bind_port_menu", "F1", "Toggle MKVDCU Port Menu", [this]() {
    if (port_menu_) {
      port_menu_->RequestClose();
    } else {
      OpenPortMenu();
    }
  });
  rex::ui::RegisterBind("bind_perf_overlay", "F2", "Toggle performance overlay", [this]() {
    const bool visible = perf_overlay_ == nullptr;
    rex::cvar::SetFlagByName("port_perf_overlay", visible ? "true" : "false");
    SavePortCvars(host_.config_path, {"port_perf_overlay"});
    ShowPerfOverlay(visible);
  });
}

void MkvsdcuApp::OnPostSetup() {
  sampler_ = std::make_unique<telemetry::SystemSampler>([this]() -> uint32_t {
    auto* graphics = static_cast<rex::graphics::GraphicsSystem*>(runtime()->graphics_system());
    auto* processor = graphics ? graphics->command_processor() : nullptr;
    return processor ? processor->counter() : 0;
  });
  host_.sampler = sampler_.get();
  host_.profiler = &profiler_;
  host_tweaks::Install();
  if (rex::cvar::Query<bool>("port_perf_csv")) sampler_->SetCsvLogging(true, host_.log_dir);
  if (const int delay = rex::cvar::Query<int32_t>("port_profile_after"); delay > 0) {
    sampler_->SetWanted("auto-profile", true);
    auto_profile_ = std::thread([this, delay] {
      for (int i = 0; i < delay * 10 && !stopping_; ++i) Sleep(100);
      if (stopping_) return;
      std::vector<std::pair<uint32_t, std::string>> threads;
      for (const auto& thread : sampler_->Latest().busiest_threads) {
        if (threads.size() < 4 && thread.core_percent >= 5) threads.emplace_back(thread.id, thread.name);
      }
      profiler_.Start(std::move(threads), 5.0, host_.log_dir);
    });
  }

  auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
  if (input) {
    input->SetActiveCallback([this]() {
      return port_menu_ == nullptr && !imgui_drawer()->GetIO().WantCaptureMouse;
    });
  }
  if (rex::cvar::Query<bool>("port_perf_overlay")) {
    app_context().CallInUIThreadDeferred([this] { ShowPerfOverlay(true); });
  }
}

void MkvsdcuApp::OnShutdown() {
  stopping_ = true;
  if (auto_profile_.joinable()) auto_profile_.join();
  rex::ui::UnregisterBind("bind_port_menu");
  rex::ui::UnregisterBind("bind_perf_overlay");
  if (port_menu_) port_menu_->SaveIfDirty();
  delete port_menu_;
  port_menu_ = nullptr;
  delete perf_overlay_;
  perf_overlay_ = nullptr;
  host_.sampler = nullptr;
  sampler_.reset();
  host_tweaks::Uninstall();
}

void MkvsdcuApp::OpenPortMenu() {
  if (port_menu_ || !imgui_drawer()) return;
  port_menu_ = new PortMenuDialog(imgui_drawer(), &host_, [this]() { port_menu_ = nullptr; });
}

void MkvsdcuApp::ShowPerfOverlay(bool visible) {
  if (visible && !perf_overlay_ && imgui_drawer()) {
    perf_overlay_ = new PerfOverlay(imgui_drawer(), &host_, [this]() { perf_overlay_ = nullptr; });
  } else if (!visible && perf_overlay_) {
    perf_overlay_->Hide();
  }
}

PortWindowInfo MkvsdcuApp::WindowInfo() const {
  PortWindowInfo info;
  if (auto* w = window()) {
    info.width = w->GetActualPhysicalWidth();
    info.height = w->GetActualPhysicalHeight();
    info.fullscreen = w->IsFullscreen();
  }
  return info;
}

void MkvsdcuApp::ResizeWindow(uint32_t width, uint32_t height) {
  // Resizing from inside the overlay's paint would re-enter the presenter.
  app_context().CallInUIThreadDeferred([this, width, height] {
    auto* w = window();
    if (!w || w->IsFullscreen()) return;
    auto hwnd = static_cast<HWND>(w->GetNativeWindowHandle());
    if (!hwnd) return;
    if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    const DWORD style = DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD ex_style = DWORD(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    RECT rect = {0, 0, LONG(width), LONG(height)};
    AdjustWindowRectExForDpi(&rect, style, FALSE, ex_style, GetDpiForWindow(hwnd));
    const int outer_w = rect.right - rect.left, outer_h = rect.bottom - rect.top;
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    const int x = work.left + std::max(0, int((work.right - work.left - outer_w) / 2));
    const int y = work.top + std::max(0, int((work.bottom - work.top - outer_h) / 2));
    SetWindowPos(hwnd, nullptr, x, y, outer_w, outer_h, SWP_NOZORDER | SWP_NOACTIVATE);
  });
}
