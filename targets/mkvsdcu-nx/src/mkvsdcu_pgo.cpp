// GCC's freestanding stream API avoids target fopen paths and exit-only dumps.
// Each save is a cumulative snapshot; the importer uses only the newest complete
// snapshot per session. Two slots retain the previous save across an interrupted
// FAT rename. Counters are never reset while game threads are running.
#include "mkvsdcu_pgo.h"
#ifdef MKVSDCU_PGO_GENERATE
#include "mkvsdcu_pgo_identity.h"
#include <switch.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
extern "C" {
#include <gcov.h>
extern const gcov_info* const __start_gcov_info[];
extern const gcov_info* const __stop_gcov_info[];
}
namespace mkvsdcu::pgo {
const char* Label() { return "PGO training (extra CPU overhead)"; }
namespace {
std::mutex dump_mutex, wake_mutex;
std::condition_variable wake;
std::thread worker;
bool stopped = false, started = false;
AppletHookCookie hook{};
std::filesystem::path session;
unsigned sequence = 0;
struct Stream {
  std::FILE* file;
  bool ok = true;
  std::vector<std::unique_ptr<unsigned char[]>> buffers;
};
void Write(const void* data, unsigned n, void* arg) {
  auto& s = *static_cast<Stream*>(arg);
  if(s.ok && n && std::fwrite(data, 1, n, s.file) != n) s.ok = false;
}
void Filename(const char* name, void* arg) { __gcov_filename_to_gcfn(name, Write, arg); }
void* Allocate(unsigned n, void* arg) {
  auto& s = *static_cast<Stream*>(arg);
  s.buffers.push_back(std::make_unique<unsigned char[]>(n ? n : 1));
  return s.buffers.back().get();
}
void Dump() {
  std::lock_guard lock(dump_mutex);
  if(session.empty()) return;
  const auto next = sequence + 1;
  const auto slot = session / (std::to_string(next % 2) + ".snapshot");
  const auto tmp = session / "pending.tmp";
  auto* file = std::fopen(tmp.string().c_str(), "wb");
  if(!file) { REXLOG_WARN("[MKvDCU PGO] Cannot open snapshot"); return; }
  std::vector<char> buffer(1024 * 1024);
  std::setvbuf(file, buffer.data(), _IOFBF, buffer.size());
  // Header is checked and stripped by the PC importer before gcov-tool sees it.
  const bool header_ok = std::fprintf(file, "MKVPGO1 %s %u\n", MKVSDCU_PGO_ID, next) > 0;
  Stream stream{file};
  auto info = __start_gcov_info;
  __asm__("" : "+r"(info));
  size_t objects = 0;
  for(; info != __stop_gcov_info && stream.ok; ++info) {
    if(*info) {
      __gcov_info_to_gcda(*info, Filename, Write, Allocate, &stream);
      stream.buffers.clear();
      ++objects;
    }
  }
  // A trailer distinguishes a complete snapshot from a transfer or write cut short.
  const bool trailer_ok = std::fwrite("MKVPGO-END\n", 1, 11, file) == 11;
  const bool closed = std::fclose(file) == 0;
  if(!header_ok || !stream.ok || !trailer_ok || !closed || !objects) {
    REXLOG_WARN("[MKvDCU PGO] Incomplete snapshot; previous save retained"); return;
  }
  std::error_code ec;
  std::filesystem::remove(slot, ec); // opposite slot is the last complete snapshot
  ec.clear();
  std::filesystem::rename(tmp, slot, ec);
  if(ec) { REXLOG_WARN("[MKvDCU PGO] Snapshot rename failed: {}", ec.message()); return; }
  sequence = next;
  REXLOG_INFO("[MKvDCU PGO] Saved snapshot {} ({} objects), training {}", sequence, objects, MKVSDCU_PGO_ID);
}
void OnApplet(AppletHookType event, void*) {
  if(event == AppletHookType_OnFocusState && appletGetFocusState() != AppletFocusState_InFocus) Dump();
}
}
void Start() {
  if(started) return;
  std::error_code ec;
  const auto folder = rex::filesystem::GetExecutableFolder() / "pgo" / MKVSDCU_PGO_ID;
  std::filesystem::create_directories(folder, ec);
  if(ec) { REXLOG_ERROR("[MKvDCU PGO] Cannot create profile folder: {}", ec.message()); return; }
  for(unsigned n=1; n<100000; ++n) {
    const auto candidate = folder / ("session-" + std::to_string(n));
    if(std::filesystem::create_directory(candidate, ec)) { session = candidate; break; }
    if(ec) { REXLOG_ERROR("[MKvDCU PGO] Cannot reserve session: {}", ec.message()); return; }
  }
  if(session.empty()) return;
  stopped = false;
  started = true;
  appletHook(&hook, OnApplet, nullptr);
  worker = std::thread([] {
    std::unique_lock lock(wake_mutex);
    while(!wake.wait_for(lock, std::chrono::seconds(60), [] { return stopped; })) {
      lock.unlock(); Dump(); lock.lock();
    }
  });
  REXLOG_INFO("[MKvDCU PGO] TRAINING build {}; saves every 60 seconds and on HOME. FPS includes recording overhead.", MKVSDCU_PGO_ID);
}
void Stop() {
  if(!started) return;
  appletUnhook(&hook);
  { std::lock_guard lock(wake_mutex); stopped = true; }
  wake.notify_all();
  if(worker.joinable()) worker.join();
  Dump();
  started = false;
  session.clear();
}
}
#else
namespace mkvsdcu::pgo {
void Start() {}
void Stop() {}
const char* Label() { return ""; }
}
#endif
