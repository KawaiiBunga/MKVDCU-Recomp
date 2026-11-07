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
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#define SCOPE_profile_cpu_f(...) ((void)0)
#define SCOPE_profile_cpu_i(...) ((void)0)
#define REXAPU_DEBUG(...) ((void)0)
#define REXAPU_INFO(...) ((void)0)
#define REXAPU_WARN(...) ((void)0)
#define REXKRNL_DEBUG(...) ((void)0)
#define assert_true(...) ((void)0)
using u32 = uint32_t;
constexpr u32 X_ERROR_SUCCESS = 0;
constexpr u32 X_E_INVALIDARG = 0x80070057;
void check(bool ok) { if (!ok) { std::fputs("audio lifetime check failed\n", stderr); std::exit(1); } }
struct mapped_void {
  u32 address;
  u32 guest_address() const { return address; }
};
std::atomic<unsigned> frames{0}, destroyed{0}, freed{0}, dumps{0};
namespace rex {
template<class T, size_t N> constexpr size_t countof(T(&)[N]) { return N; }
}
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
using GanchoAntesDeTrama = void(*)(unsigned);
std::atomic<GanchoAntesDeTrama> g_gancho_antes_de_trama{nullptr};
struct Dispatcher {
  std::function<void()> execute;
  void Execute(void*, u32, uint64_t*, size_t) { execute(); }
};
struct Worker { void* thread_state() { return nullptr; } };
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
  std::recursive_mutex client_callback_mutex_;
  u32 active_callback_arg_ = 0, retired_callback_arg_ = 0;
  Dispatcher dispatcher;
  Dispatcher* function_dispatcher_ = &dispatcher;
  Worker worker;
  Worker* worker_thread_ = &worker;
  std::unique_ptr<rex::thread::Semaphore> client_semaphores_[kMaximumClientCount];
  AudioSystem() {
    for (auto& semaphore : client_semaphores_)
      semaphore = std::make_unique<rex::thread::Semaphore>();
  }
  Memory* memory() { return &guest_memory; }
  void DestroyDriver(AudioDriver* driver) { check(driver != nullptr); delete driver; }
  void SubmitFrame(size_t, u32);
  void UnregisterClient(size_t);
  void PumpClient(size_t, u32&);
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

  // Reproduce the shutdown interleaving: retirement must not return while
  // the callback can still change the guest's engine-busy flag.
  audio.Activate();
  std::atomic<bool> entered{false}, may_finish{false}, retiring{false}, retired{false};
  std::atomic<bool> engine_busy{false};
  const unsigned before_retire = freed;
  audio.dispatcher.execute = [&] {
    engine_busy = true;
    entered = true;
    while (!may_finish) std::this_thread::yield();
    // Retirement must release the global region while waiting, or this
    // real callback's submission deadlocks against it.
    audio.SubmitFrame(0, 123);
    check(freed == before_retire);
    engine_busy = false;
  };
  u32 pump_count = 0;
  std::thread callback([&] { audio.PumpClient(0, pump_count); });
  while (!entered) std::this_thread::yield();
  std::thread retire([&] {
    retiring = true;
    audio.UnregisterClient(0);
    check(!engine_busy);
    retired = true;
  });
  while (!retiring) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  check(!retired && freed == before_retire);
  may_finish = true;
  callback.join(); retire.join();
  check(retired && pump_count == 1 && freed == before_retire+1);
  audio.PumpClient(0, pump_count);  // A stale semaphore may not dispatch again.
  check(pump_count == 1);

  // Self-unregistration cannot wait on itself or free its callback argument
  // before Execute returns; the production epilogue reclaims it exactly once.
  audio.Activate();
  const unsigned before_self = freed;
  audio.dispatcher.execute = [&] {
    audio.UnregisterClient(0);
    check(freed == before_self);
    audio.UnregisterClient(0);
    check(freed == before_self);
  };
  audio.PumpClient(0, pump_count);
  check(pump_count == 2 && freed == before_self+1);

  // Exercise selection/retirement races repeatedly. Once retirement returns,
  // no new callback may start, even with leftover semaphore tokens.
  for (unsigned round=0; round<100; ++round) {
    audio.Activate();
    std::atomic<unsigned> calls{0};
    audio.dispatcher.execute = [&] { ++calls; audio.SubmitFrame(0, 123); };
    std::thread racing([&] { u32 n=0; for(unsigned i=0;i<100;++i) audio.PumpClient(0,n); });
    audio.UnregisterClient(0);
    const unsigned after = calls;
    racing.join();
    check(calls == after);
  }
  std::puts("PASS: zero/invalid handles, inactive and out-of-range slots, live delivery, "
            "retired submission, duplicate retirement, concurrent submission/retirement, "
            "in-flight callback quiescence, unlocked callback completion, deferred self-retirement, "
            "100 callback-selection/retirement races");
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
            'void AudioSystem::SubmitFrame(', 'void AudioSystem::UnregisterClient(',
            'void AudioSystem::PumpClient(')))
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
