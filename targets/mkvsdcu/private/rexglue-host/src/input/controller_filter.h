#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rex/input/input_system.h>

// Controller settings applied between the physical pad and the game:
// deadzones, button remapping, rumble strength, stick-as-D-pad, and a
// Back+Start chord that opens the port menu.
namespace port_input {

constexpr const char* kControlNames[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT"};
constexpr const char* kMapCvars[] = {"pad_map_a",  "pad_map_b",  "pad_map_x",  "pad_map_y",
                                     "pad_map_lb", "pad_map_rb", "pad_map_lt", "pad_map_rt"};

// Raw state of a physical controller, before any port setting is applied.
struct PadSnapshot {
  std::string name;
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0, thumb_ly = 0, thumb_rx = 0, thumb_ry = 0;
  uint32_t age_ms = 0;  // since the game last polled it
};

// Mirrors rex::input::CreateDefaultInputSystem with the controller drivers
// wrapped. `on_menu_chord` runs on the polling thread.
std::unique_ptr<rex::input::InputSystem> CreateInputSystem(bool tool_mode,
                                                           std::function<void()> on_menu_chord);

std::vector<PadSnapshot> ConnectedPads();

}  // namespace port_input
