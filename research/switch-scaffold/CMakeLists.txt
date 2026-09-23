cmake_minimum_required(VERSION 3.25)
project(rexglue_switch_template LANGUAGES C CXX)

# This is an architecture template, not a claimed ReXGlue Switch port.  The
# official SDK presently ships host presets only; do not force generated PPC
# code through devkitA64 until its ABI/runtime requirements are demonstrated.
option(REXGLUE_SWITCH_ENABLE_EXPERIMENTAL "Attempt an unproven ReXGlue Switch integration" OFF)
if(REXGLUE_SWITCH_ENABLE_EXPERIMENTAL)
  message(FATAL_ERROR "No ReXGlue-to-libnx game runtime integration exists yet. See docs/switch/RESEARCH.md.")
endif()

add_library(rexglue_switch_interfaces INTERFACE)
target_include_directories(rexglue_switch_interfaces INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/input)
