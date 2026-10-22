#include "mkvsdcu_port_menu.h"
#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <fstream>
#include <utility>
#include <sstream>

MkvsdcuPortMenu::MkvsdcuPortMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config,
    std::function<rex::ui::FrameStats()> stats, std::function<void()> resume,
    std::function<void()> advanced, std::map<std::string, std::string>& pending)
    : SettingsDialog(drawer, config), config_(std::move(config)), pending_(pending), stats_(std::move(stats)),
      resume_(std::move(resume)), advanced_(std::move(advanced)) { restart_pending_ = !pending_.empty(); }

bool MkvsdcuPortMenu::Set(const char* name, const std::string& value, bool restart) {
  const auto* info = rex::cvar::GetFlagInfo(name);
  if (restart && info && info->lifecycle == rex::cvar::Lifecycle::kInitOnly) {
    // Do not modify device-selection cvars after init. Save a next-launch
    // override instead; the app owns it across menu instances.
    pending_[name] = value;
  } else if (!rex::cvar::SetFlagByName(name, value)) {
    status_ = std::string("Could not change ") + name;
    return false;
  }
  restart_pending_ |= restart;
  status_ = restart ? "Changed for the next launch. Save settings before restarting."
                    : "Setting applied. Save settings to keep it after restarting.";
  REXLOG_INFO("[port-menu] {} = {}{}", name, value, restart ? " (restart required)" : "");
  return true;
}

void MkvsdcuPortMenu::Toggle(const char* label, const char* name, bool restart) {
  if (!rex::cvar::GetFlagInfo(name)) return;
  bool value = rex::cvar::GetFlagByName(name) == "true";
  if (ImGui::Checkbox(label, &value)) Set(name, value ? "true" : "false", restart);
}

void MkvsdcuPortMenu::Save() {
  // Stage the config first. FAT may reject rename over an existing file,
  // so retain a backup before using the overwrite fallback.
  auto temporary = config_;
  temporary += ".port-menu.tmp";
  std::error_code ec;
  std::filesystem::remove(temporary, ec);
  std::string content;
  std::istringstream current(rex::cvar::SerializeToTOML());
  std::string line;
  while (std::getline(current, line)) {
    const auto equals = line.find(" = ");
    if (equals == std::string::npos || !pending_.contains(line.substr(0, equals)))
      content += line + "\n";
  }
  for (const auto& [name, value] : pending_) {
    // The curated init-only override is the validated fsi/fbo renderer choice.
    content += name + " = \"" + value + "\"\n";
  }
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << "# MKvDCU port menu configuration\n" << content;
    file.close();
    if (!file) {
      status_ = "Could not write settings. Previous configuration kept.";
      return;
    }
  }
  ec.clear();
  if (!std::filesystem::exists(temporary, ec) || std::filesystem::file_size(temporary, ec) == 0 || ec) {
    status_ = "Could not save settings. Check that the SD card is writable.";
    return;
  }
  std::filesystem::rename(temporary, config_, ec);
  if (ec && std::filesystem::exists(config_)) {
    auto backup = config_;
    backup += ".before-port-menu";
    ec.clear();
    std::filesystem::copy_file(config_, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) {
      std::filesystem::copy_file(temporary, config_, std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        const std::string error = ec.message();
        std::error_code restore_error;
        std::filesystem::copy_file(backup, config_, std::filesystem::copy_options::overwrite_existing, restore_error);
        status_ = "Could not save config: " + error + (restore_error ? ". Backup: " + backup.string() : ". Previous config restored.");
        return;
      }
      std::error_code cleanup_error;
      std::filesystem::remove(temporary, cleanup_error);
    }
  }
  status_ = ec ? "Could not replace the config: " + ec.message()
               : "Settings saved to mkvsdcu.toml.";
}

void MkvsdcuPortMenu::OnDraw(ImGuiIO& io) {
  bool open = true;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .06f, io.DisplaySize.y * .06f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * .88f, io.DisplaySize.y * .88f), ImGuiCond_Always);
  ImGui::Begin("MKvDCU - Port menu", &open,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
  if (stats_) {
    const auto stats = stats_();
    if (stats.fps > 0) ImGui::Text("Game: %.1f FPS  |  %.1f ms/frame", stats.fps, stats.frame_time_ms);
    else ImGui::TextUnformatted("Waiting for game frames...");
  }
  ImGui::TextDisabled("L + R + Right: toggle menu   |   D-pad: navigate   |   %s",
                      io.ConfigNavSwapGamepadButtons ? "A: select   |   B: back" : "B: select   |   A: back");
  if (focus_first_) ImGui::SetKeyboardFocusHere();
  if (ImGui::Button("Resume game")) resume_();
  if (focus_first_) { ImGui::SetItemDefaultFocus(); focus_first_ = false; }
  ImGui::SameLine();
  if (ImGui::Button("Save settings")) Save();
  ImGui::SameLine();
  if (ImGui::Button("Advanced settings")) advanced_();
  if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
  if (restart_pending_) ImGui::TextColored(ImVec4(1.f,.75f,.3f,1.f), "Some changes need a restart after saving.");
  ImGui::Separator();

  if (ImGui::BeginTabBar("port-tabs")) {
    if (ImGui::BeginTabItem("Graphics")) {
      ImGui::TextUnformatted("Renderer for next launch (restart required)");
      const auto desired = pending_.find("render_target_path_vulkan");
      const bool accurate = (desired == pending_.end() ? rex::cvar::GetFlagByName("render_target_path_vulkan") : desired->second) != "fbo";
      if (ImGui::RadioButton("Accurate - complete fighting surfaces", accurate))
        Set("render_target_path_vulkan", "fsi", true);
      if (ImGui::RadioButton("Fast - experimental", !accurate))
        Set("render_target_path_vulkan", "fbo", true);
      ImGui::TextWrapped("Accurate preserves the hangar floor, but costs more GPU time. Fast uses host render targets and can omit floors and other surfaces.");
      Toggle("Skip draws until their shader is ready", "vulkan_async_skip_incomplete_frames");
      ImGui::TextWrapped("Antialiasing stays disabled for NVK stability. Shader skipping reduces compilation pauses, but newly encountered objects can appear late.");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Performance")) {
      const bool normal = rex::cvar::GetFlagByName("vsync") == "true";
      const std::string limit = rex::cvar::GetFlagByName("gpu_frame_rate_limit");
      if (ImGui::RadioButton("Normal speed - up to 60 FPS", normal && limit == "60")) {
        Set("vsync", "true"); Set("gpu_frame_rate_limit", "60");
      }
      if (ImGui::RadioButton("30 FPS ceiling", normal && limit == "30")) {
        Set("vsync", "true"); Set("gpu_frame_rate_limit", "30");
      }
      if (ImGui::RadioButton("Unlocked - game speed can change", !normal)) Set("vsync", "false");
      ImGui::TextWrapped("A ceiling limits fast frames; it cannot make a slow scene reach the target.");
      Toggle("Let the CPU rest while waiting for the GPU", "mkvsdcu_espera_anillo_bloqueante");
      if (rex::cvar::GetFlagInfo("mkvsdcu_espera_anillo_us")) {
        int wait = std::stoi(rex::cvar::GetFlagByName("mkvsdcu_espera_anillo_us"));
        if (ImGui::SliderInt("GPU wait interval (microseconds)", &wait, 20, 1000))
          Set("mkvsdcu_espera_anillo_us", std::to_string(wait));
      }
      ImGui::Separator();
      ImGui::TextUnformatted("Handheld GPU request (restart required)");
      const std::string gpu = rex::cvar::GetFlagByName("mkvsdcu_switch_gpu_mhz");
      if (ImGui::RadioButton("Keep console clock profile", gpu == "0")) Set("mkvsdcu_switch_gpu_mhz", "0", true);
      if (ImGui::RadioButton("Official 384 MHz mode", gpu == "384")) Set("mkvsdcu_switch_gpu_mhz", "384", true);
      if (ImGui::RadioButton("Official 460.8 MHz mode", gpu == "460")) Set("mkvsdcu_switch_gpu_mhz", "460", true);
      ImGui::TextWrapped("Uses Horizon's official handheld modes. Memory clocks stay at their starting value; unsupported modes are rejected. Keep your clock profile if sys-clk manages it.");
      Toggle("Log ring-wait statistics", "mkvsdcu_ring_wait_diagnostics");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio and input")) {
      Toggle("Mute audio", "audio_mute");
      Toggle("Xbox face-button positions", "input_xbox_layout");
      ImGui::TextWrapped("With Xbox positions enabled, the bottom face button selects and the right face button goes back. Otherwise A selects and B goes back.");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Diagnostics")) {
      ImGui::TextWrapped("L + R + Up: performance overlay. L + R + Down: log console. L + R + Left: achievements.");
      Toggle("Detailed thread watchdog snapshots", "mkvsdcu_watchdog_diagnostics");
      ImGui::TextWrapped("Routine thread snapshots and ring-wait logging are off during play. The hang watchdog still reports a stall. Advanced settings exposes the SDK's full settings and configuration controls.");
      ImGui::TextWrapped("Measure the same fight scene at the same console clocks. PC frame rates do not predict Switch performance.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  const ImGuiKey cancel = io.ConfigNavSwapGamepadButtons ? ImGuiKey_GamepadFaceDown : ImGuiKey_GamepadFaceRight;
  if (!open || (!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(cancel))) resume_();
  ImGui::End();
}
