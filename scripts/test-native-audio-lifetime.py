"""Exercise the SDK's actual audio submission/unregistration bodies with fake devices.

The small host shim avoids requiring Switch services. Source bodies are copied
verbatim, including their release-build checks; no substitute implementation is
tested. All generated files and compiler output stay under E:/tools/tmp.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]


SHIM = r'''
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#define SCOPE_profile_cpu_f(...) ((void)0)
#define REXAPU_DEBUG(...) ((void)0)
#define REXAPU_INFO(...) ((void)0)
#define REXAPU_WARN(...) ((void)0)
#define REXKRNL_DEBUG(...) ((void)0)
#define assert_true(...) ((void)0)
using u32 = uint32_t;
constexpr u32 X_ERROR_SUCCESS = 0;
constexpr u32 X_E_INVALIDARG = 0x80070057;
void check(bool ok) { if (!ok) std::abort(); }
struct mapped_void {
  u32 address;
  u32 guest_address() const { return address; }
};
std::atomic<unsigned> frames{0}, destroyed{0}, freed{0}, dumps{0};
namespace rex::thread {
struct Semaphore {};
enum class WaitResult { kSuccess, kTimeout };
WaitResult Wait(Semaphore*, bool, std::chrono::milliseconds) { return WaitResult::kTimeout; }
}
struct Region {
  std::recursive_mutex mutex;
  auto Acquire() { return std::unique_lock<std::recursive_mutex>(mutex); }
};
struct Memory {
  void SystemHeapFree(u32) { ++freed; }
  template<class T> T TranslateVirtual(u32) { return nullptr; }
} guest_memory;
namespace rex::audio {
struct AudioDriver {
  virtual ~AudioDriver() { ++destroyed; }
  virtual void SubmitFrame(u32) { ++frames; }
};
struct AudioSystem {
  static constexpr size_t kMaximumClientCount = 8;
  struct Client {
    AudioDriver* driver;
    u32 callback, callback_arg, wrapped_callback_arg;
    bool in_use;
  } clients_[kMaximumClientCount]{};
  Region global_critical_region_;
  std::unique_ptr<rex::thread::Semaphore> client_semaphores_[kMaximumClientCount];
  AudioSystem() {
    for (auto& semaphore : client_semaphores_)
      semaphore = std::make_unique<rex::thread::Semaphore>();
  }
  Memory* memory() { return &guest_memory; }
  void DestroyDriver(AudioDriver* driver) { check(driver != nullptr); delete driver; }
  void SubmitFrame(size_t, u32);
  void UnregisterClient(size_t);
  void Activate() { clients_[0] = {new AudioDriver, 1, 2, 3, true}; }
};
// PRODUCTION_AUDIO_BODIES
}
rex::audio::AudioSystem* active_audio;
namespace rex::system {
struct Emulator { rex::audio::AudioSystem* audio_system() { return active_audio; } };
struct Kernel { Emulator* emulator() { static Emulator e; return &e; } } kernel;
}
#define REX_KERNEL_STATE() (&rex::system::kernel)
#define REX_KERNEL_MEMORY() (&guest_memory)
void VolcarTrama(u32, const uint8_t*) { ++dumps; }
namespace rex::kernel::xboxkrnl {
using namespace rex::system;
// PRODUCTION_KERNEL_BODIES
}
int main() {
  using namespace rex::kernel::xboxkrnl;
  rex::audio::AudioSystem audio;
  active_audio = &audio;
  check(XAudioSubmitRenderDriverFrame_entry({0}, {123}) == X_E_INVALIDARG);
  check(XAudioSubmitRenderDriverFrame_entry({0x12340000}, {123}) == X_E_INVALIDARG);
  check(XAudioUnregisterRenderDriverClient_entry({0}) == X_E_INVALIDARG);
  check(frames == 0 && destroyed == 0 && dumps == 0);
  audio.SubmitFrame(0, 123);  // inactive slot
  audio.SubmitFrame(65535, 123);  // bounds must be checked before array access
  audio.UnregisterClient(65535);
  audio.Activate();
  check(XAudioSubmitRenderDriverFrame_entry({0x41550000}, {123}) == X_ERROR_SUCCESS);
  check(frames == 1);
  check(XAudioUnregisterRenderDriverClient_entry({0x41550000}) == X_ERROR_SUCCESS);
  check(destroyed == 1 && freed == 1);
  XAudioSubmitRenderDriverFrame_entry({0x41550000}, {123});  // stale signed handle
  XAudioUnregisterRenderDriverClient_entry({0x41550000});  // duplicate retirement
  check(frames == 1 && destroyed == 1 && freed == 1);
  audio.Activate();
  std::thread submitter([&] {
    for (int i = 0; i < 100000; ++i)
      XAudioSubmitRenderDriverFrame_entry({0x41550000}, {123});
  });
  while (frames == 1) std::this_thread::yield();
  XAudioUnregisterRenderDriverClient_entry({0x41550000});
  submitter.join();
  const unsigned submitted = frames;
  audio.SubmitFrame(0, 123);
  check(frames == submitted && destroyed == 2 && freed == 2);
  std::puts("PASS: zero/invalid handles, inactive and out-of-range slots, live delivery, "
            "retired submission, duplicate retirement, concurrent submission/retirement");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path('references/mkvsdcu-deko-sdk'))
    parser.add_argument('--compiler', type=Path,
        default=Path('E:/tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'))
    args = parser.parse_args()
    audio = (args.sdk/'src/audio/audio_system.cpp').read_text()
    kernel = (args.sdk/'src/kernel/xboxkrnl/xboxkrnl_audio.cpp').read_text()
    code = SHIM.replace('// PRODUCTION_AUDIO_BODIES', '\n'.join(
        function(audio, signature) for signature in (
            'void AudioSystem::SubmitFrame(', 'void AudioSystem::UnregisterClient(')))
    code = code.replace('// PRODUCTION_KERNEL_BODIES', '\n'.join(
        function(kernel, signature) for signature in (
            'u32 XAudioSubmitRenderDriverFrame_entry(',
            'u32 XAudioUnregisterRenderDriverClient_entry(')))
    with tempfile.TemporaryDirectory(prefix='mkvsdcu-audio-', dir='E:/tools/tmp') as folder:
        folder = Path(folder)
        source, executable = folder/'audio_test.cpp', folder/'audio_test.exe'
        source.write_text(code)
        subprocess.run([str(args.compiler), '-std=c++20', '-O2', '-DNDEBUG',
            '-static', '-pthread', str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
