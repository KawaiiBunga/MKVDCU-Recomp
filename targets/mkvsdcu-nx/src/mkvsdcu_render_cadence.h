#pragma once
#include <cstdint>

// One original 1/60 simulation step on every engine iteration; one viewport
// submission per pair. Time is supplied by the caller for deterministic tests.
class MkvsdcuRenderCadence {
 public:
  struct Frame { bool active; bool draw; int64_t wait_ns; };
  static constexpr int64_t kPairNs = 1000000000LL / 30;
  Frame Begin(int64_t now_ns, bool eligible) {
    if (!eligible) { Reset(); return {false, true, 0}; }
    if (!started_) { started_ = true; deadline_ = now_ns; }
    const bool draw = (frames_++ & 1) == 0;
    int64_t wait = 0;
    if (draw) {
      // Do not build a catch-up backlog after loading, suspending, or a stall.
      if (now_ns > deadline_ + kPairNs) deadline_ = now_ns;
      wait = deadline_ > now_ns ? deadline_ - now_ns : 0;
      deadline_ += kPairNs;
    }
    return {true, draw, wait};
  }
  void Reset() { started_ = false; frames_ = 0; deadline_ = 0; }
 private:
  bool started_ = false;
  uint64_t frames_ = 0;
  int64_t deadline_ = 0;
};
