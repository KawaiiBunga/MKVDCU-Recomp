#include "mkvsdcu_port_menu.h"
#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <fstream>
#include <utility>
#include <sstream>
#include <iomanip>
#include <toml++/toml.hpp>

MkvsdcuPortMenu::MkvsdcuPortMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config,
    std::function<rex::ui::FrameStats()> stats, std::function<void()> resume,
    std::function<void()> advanced, std::map<std::string, std::string>& pending,
    std::function<void()> settings_changed)
    : SettingsDialog(drawer, config), config_(std::move(config)), pending_(pending), stats_(std::move(stats)),
      resume_(std::move(resume)), advanced_(std::move(advanced)), settings_changed_(std::move(settings_changed)) { restart_pending_ = !pending_.empty(); }

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
  if (settings_changed_) settings_changed_();
  restart_pending_ |= restart;
  status_ = restart ? "Changed for the next launch. Save settings before restarting."
                    : "Setting applied. Save settings to keep it after restarting.";
  REXLOG_INFO("[port-menu] {} = {}{}", name, value, restart ? " (restart required)" : "");
  return true;
}

void MkvsdcuPortMenu::Toggle(const char* label, const char* name, bool restart) {
  if (!rex::cvar::GetFlagInfo(name)) return;
  bool value = Value(name) == "true";
  if (ImGui::Checkbox(label, &value)) Set(name, value ? "true" : "false", restart);
}

std::string MkvsdcuPortMenu::Value(const char* name) const {
  const auto it = pending_.find(name);
  return it == pending_.end() ? rex::cvar::GetFlagByName(name) : it->second;
}

void MkvsdcuPortMenu::Save() {
  // Stage the config first. FAT may reject rename over an existing file,
  // so retain a backup before using the overwrite fallback.
  auto temporary = config_;
  temporary += ".port-menu.tmp";
  std::error_code ec;
  std::filesystem::remove(temporary, ec);
  // Preserve unrelated values and tables (game paths, shader settings, saves).
  // Read both documents before touching the existing file, and retain TOML types.
  std::string content;
  try {
    toml::table document;
    if (std::filesystem::exists(config_)) document = toml::parse_file(config_.string());
    const auto live = toml::parse(rex::cvar::SerializeToTOML());
    constexpr const char* keys[] = {"mkvsdcu_scene_scale", "mkvsdcu_fps_limit", "mkvsdcu_show_fps", "mkvsdcu_30fps_timing", "mkvsdcu_30fps_render",
      "input_xbox_layout", "mkvsdcu_stick_deadzone", "mkvsdcu_swap_sticks", "audio_mute"};
    for (const char* key : keys) {
      if (const auto it = pending_.find(key); it != pending_.end()) {
        // Curated restart-only choices retain their integer or boolean type.
        auto value = toml::parse(std::string(key) + " = " + it->second);
        document.insert_or_assign(key, *value.get(key));
      } else if (const auto* value = live.get(key)) document.insert_or_assign(key, *value);
    }
    std::ostringstream output;
    output << document;
    content = output.str();
  } catch (const toml::parse_error&) {
    status_ = "Could not read settings. Existing configuration kept.";
    return;
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
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .06f, io.DisplaySize.y * .06f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * .88f, io.DisplaySize.y * .88f), ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.075f,.08f,.09f,.98f));
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(.38f,.27f,.18f,1.f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.25f,.20f,.15f,1.f));
  ImGui::Begin("MKvDCU | PORT SETTINGS", nullptr,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
  ImGui::TextColored(ImVec4(.73f,.64f,.49f,1.f), "MORTAL KOMBAT vs. DC UNIVERSE");
  if (stats_) {
    const auto stats = stats_();
    ImGui::Text("%.1f FPS  |  %.1f ms/frame", stats.fps, stats.frame_time_ms);
  }
  ImGui::TextDisabled("+ and -: toggle  |  D-pad: navigate  |  L/R: tabs  |  %s",
      io.ConfigNavSwapGamepadButtons ? "A: select, B: back" : "B: select, A: back");
  if (focus_first_) ImGui::SetKeyboardFocusHere();
  if (ImGui::Button("Resume game")) resume_();
  if (focus_first_) { ImGui::SetItemDefaultFocus(); focus_first_ = false; }
  ImGui::SameLine();
  if (ImGui::Button("Save settings")) Save();
  if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
  if (restart_pending_) ImGui::TextColored(ImVec4(1.f,.75f,.3f,1.f), "Save and restart to apply resolution/timing changes.");
  ImGui::Separator();
  int requested_tab = -1;
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) requested_tab = (active_tab_ + 3) % 4;
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) requested_tab = (active_tab_ + 1) % 4;
  auto tab = [&](const char* title, int index) {
    const bool shown = ImGui::BeginTabItem(title, nullptr,
        requested_tab == index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
    if (shown) active_tab_ = index;
    return shown;
  };
  if (ImGui::BeginTabBar("port-tabs")) {
    if (tab("Display", 0)) {
#if MKVSDCU_DEKO3D
      ImGui::TextColored(ImVec4(.73f,.64f,.49f,1.f), "SCENE RESOLUTION");
      const std::string scale = Value("mkvsdcu_scene_scale");
      if (ImGui::RadioButton("100% | Original scene (1040 x 624)", scale == "100")) Set("mkvsdcu_scene_scale", "100", true);
      if (ImGui::RadioButton("75% | 780 x 468", scale == "75")) Set("mkvsdcu_scene_scale", "75", true);
      if (ImGui::RadioButton("50% | 520 x 312", scale == "50")) Set("mkvsdcu_scene_scale", "50", true);
      ImGui::TextWrapped("Lower scene resolution reduces GPU pixel work. Final output and this menu stay at 1280 x 720. Applies after saving and restarting.");
#endif
      ImGui::Separator();
      ImGui::TextColored(ImVec4(.73f,.64f,.49f,1.f), "FRAME RATE");
#if MKVSDCU_DEKO3D
      Toggle("30 FPS rendering / full-speed target (restart)", "mkvsdcu_30fps_render", true);
      const bool timing30 = Value("mkvsdcu_30fps_render") == "true";
      if (timing30) ImGui::TextWrapped("Experimental: keeps the original 60 Hz game updates and draws every other frame. Save and restart. Full speed requires enough CPU time for both updates.");
      ImGui::BeginDisabled(timing30);
#endif
      const std::string fps = Value("mkvsdcu_fps_limit");
      if (ImGui::RadioButton("60 FPS cap", fps == "60")) Set("mkvsdcu_fps_limit", "60");
      if (ImGui::RadioButton("30 FPS cap", fps == "30")) Set("mkvsdcu_fps_limit", "30");
      if (ImGui::RadioButton("Uncapped", fps == "0")) Set("mkvsdcu_fps_limit", "0");
#if MKVSDCU_DEKO3D
      ImGui::EndDisabled();
#endif
      Toggle("Show FPS counter", "mkvsdcu_show_fps");
      ImGui::TextDisabled("Frame caps alone do not change gameplay timing.");
      ImGui::EndTabItem();
    }
    if (tab("Controls", 1)) {
      Toggle("Xbox face-button positions", "input_xbox_layout");
      ImGui::TextWrapped(Value("input_xbox_layout") == "true"
        ? "Switch B -> Xbox A, A -> B, Y -> X, X -> Y."
        : "Switch A -> Xbox A, B -> B, X -> X, Y -> Y.");
      ImGui::TextUnformatted("L / R -> LB / RB    ZL / ZR -> LT / RT");
      ImGui::TextUnformatted("+ -> Start    - -> Back    + and - -> Port menu");
      int deadzone = std::stoi(Value("mkvsdcu_stick_deadzone"));
      if (ImGui::SliderInt("Stick deadzone", &deadzone, 0, 40, "%d%%")) Set("mkvsdcu_stick_deadzone", std::to_string(deadzone));
      Toggle("Swap left and right sticks", "mkvsdcu_swap_sticks");
      ImGui::TextDisabled("Controls apply immediately to all connected players.");
      ImGui::EndTabItem();
    }
    if (tab("Audio", 2)) {
      Toggle("Mute audio", "audio_mute");
      ImGui::EndTabItem();
    }
    if (tab("About", 3)) {
      ImGui::TextUnformatted("Nintendo Switch | Native deko3d renderer");
      ImGui::TextWrapped("Port settings are saved beside the NRO in mkvsdcu.toml. Use the game's own pause menu before changing settings during a fight.");
      ImGui::TextWrapped("L + R + Up: detailed performance overlay. L + R + Down: log console.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  const ImGuiKey cancel = io.ConfigNavSwapGamepadButtons ? ImGuiKey_GamepadFaceDown : ImGuiKey_GamepadFaceRight;
  if (!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(cancel)) resume_();
  ImGui::End();
  ImGui::PopStyleColor(3);
}
