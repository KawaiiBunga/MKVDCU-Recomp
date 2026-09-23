#pragma once

#include <cstdint>
#include <vector>

// Timing of the frames the game itself presents, measured at its VdSwap call.
// This is the game's real frame rate, independent of how often the host
// window repaints.
namespace telemetry {

struct FrameStats {
  uint64_t total_frames = 0;
  uint32_t samples = 0;  // intervals inside the window
  double fps = 0;
  double avg_ms = 0;
  double min_ms = 0;
  double max_ms = 0;
  double p99_ms = 0;   // 1% of frames were slower than this
  double p999_ms = 0;  // 0.1%
  double stddev_ms = 0;
  uint32_t hitches = 0;  // frames longer than 1.5x the median
  double seconds_since_last_frame = 0;
};

// Size of the image the game hands to VdSwap (before host render scaling).
struct GuestOutputSize {
  uint32_t width = 0;
  uint32_t height = 0;
};

FrameStats ComputeFrameStats(double window_seconds);
GuestOutputSize LastGuestOutputSize();
uint64_t GuestFrameCount();

// Most recent frame intervals in milliseconds, oldest first.
void RecentFrameTimes(std::vector<float>& out, size_t max_count);

}  // namespace telemetry
