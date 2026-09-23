#pragma once

namespace rexglue_switch {
struct GuitarState {
  bool green = false;
  bool red = false;
  bool yellow = false;
  bool blue = false;
  bool orange = false;
  bool strumUp = false;
  bool strumDown = false;
  bool start = false;
  bool select = false;
  float whammy = 0.0F;
  float tilt = 0.0F;
};

// Intentionally separate from normal controller input. Future adapters may
// consume libnx HID, USB HID, or a device-specific dongle protocol.
class GuitarInputProvider {
 public:
  virtual ~GuitarInputProvider() = default;
  virtual bool Poll(GuitarState& state) = 0;
};
}  // namespace rexglue_switch

