// Standalone bring-up test. This deliberately does not claim to run the game.
#include <switch.h>
#include <deko3d.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "probe_shaders.h"

namespace {
constexpr uint32_t kWidth = 1280, kHeight = 720, kFrames = 3;
constexpr uint32_t kCommandBytes = 64 * 1024;
constexpr uint32_t kCommandSlack = 256;  // deko3d 0.5 under-reservation workaround.
FILE* log_file;

void Fail(const char* why) {
  if (log_file) {
    std::fprintf(log_file, "FAIL %s\n", why);
    std::fflush(log_file);
  }
  std::abort();
}

void Debug(void*, const char* context, DkResult result, const char* message) {
  if (log_file) {
    std::fprintf(log_file, "deko3d %s result=%d %s\n", context, int(result), message);
    std::fflush(log_file);
  }
  if (result != DkResult_Success) Fail("deko3d diagnostic error");
}

DkMemBlock Memory(DkDevice device, uint32_t bytes, uint32_t flags) {
  DkMemBlockMaker maker;
  dkMemBlockMakerDefaults(&maker, device, (bytes + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1));
  maker.flags = flags;
  return dkMemBlockCreate(&maker);
}

uint32_t AlignCode(uint32_t value) {
  return (value + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
}
}  // namespace

int main(int, char**) {
  log_file = std::fopen("sdmc:/mkvsdcu-deko3d-probe.log", "w");
  if (log_file) {
    std::fprintf(log_file, "Native deko3d platform probe; no game renderer.\n"
                           "Expected: red top-left, green top-right, blue bottom-left, white bottom-right.\n");
    std::fflush(log_file);
  }
  DkDeviceMaker dm;
  dkDeviceMakerDefaults(&dm);
  dm.flags = DkDeviceFlags_OriginUpperLeft | DkDeviceFlags_DepthZeroToOne;
  dm.cbDebug = Debug;
  DkDevice device = dkDeviceCreate(&dm);

  DkImageLayoutMaker lm;
  dkImageLayoutMakerDefaults(&lm, device);
  lm.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent;
  lm.format = DkImageFormat_RGBA8_Unorm;
  lm.dimensions[0] = kWidth;
  lm.dimensions[1] = kHeight;
  DkImageLayout layout;
  dkImageLayoutInitialize(&layout, &lm);
  const uint32_t alignment = dkImageLayoutGetAlignment(&layout);
  const uint32_t stride = (dkImageLayoutGetSize(&layout) + alignment - 1) & ~(alignment - 1);
  DkMemBlock image_memory = Memory(device, stride * kFrames, DkMemBlockFlags_Image | DkMemBlockFlags_GpuCached);
  DkImage images[kFrames];
  const DkImage* image_ptrs[kFrames];
  for (uint32_t i = 0; i < kFrames; ++i) {
    dkImageInitialize(&images[i], &layout, image_memory, i * stride);
    image_ptrs[i] = &images[i];
  }
  DkSwapchainMaker sm;
  dkSwapchainMakerDefaults(&sm, device, nwindowGetDefault(), image_ptrs, kFrames);
  DkSwapchain swapchain = dkSwapchainCreate(&sm);
  dkSwapchainSetSwapInterval(swapchain, 1);

  const uint32_t fragment_offset = AlignCode(sizeof(shader_vert));
  const uint32_t code_bytes = fragment_offset + AlignCode(sizeof(shader_frag)) + DK_SHADER_CODE_UNUSABLE_SIZE;
  DkMemBlock code = Memory(device, code_bytes,
      DkMemBlockFlags_Code | DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
  auto* code_cpu = static_cast<unsigned char*>(dkMemBlockGetCpuAddr(code));
  std::memcpy(code_cpu, shader_vert, sizeof(shader_vert));
  std::memcpy(code_cpu + fragment_offset, shader_frag, sizeof(shader_frag));
  DkShader vertex, fragment;
  DkShaderMaker shader_maker;
  dkShaderMakerDefaults(&shader_maker, code, 0);
  dkShaderInitialize(&vertex, &shader_maker);
  dkShaderMakerDefaults(&shader_maker, code, fragment_offset);
  dkShaderInitialize(&fragment, &shader_maker);

  DkMemBlock commands = Memory(device, kCommandBytes,
      DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
  DkCmdBufMaker cm;
  dkCmdBufMakerDefaults(&cm, device);
  DkCmdBuf cmd = dkCmdBufCreate(&cm);
  dkCmdBufAddMemory(cmd, commands, 0, kCommandBytes - kCommandSlack);
  DkCmdList lists[kFrames];
  for (uint32_t i = 0; i < kFrames; ++i) {
    DkImageView view;
    dkImageViewDefaults(&view, &images[i]);
    dkCmdBufBindRenderTarget(cmd, &view, nullptr);
    const DkViewport viewport{0, 0, float(kWidth), float(kHeight), 0, 1};
    const DkScissor scissor{0, 0, kWidth, kHeight};
    dkCmdBufSetViewports(cmd, 0, &viewport, 1);
    dkCmdBufSetScissors(cmd, 0, &scissor, 1);
    dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA, 0, 0, 0, 1);
    const DkShader* shaders[] = {&vertex, &fragment};
    dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
    DkRasterizerState raster;
    DkColorState color;
    DkColorWriteState write;
    dkRasterizerStateDefaults(&raster);
    raster.cullMode = DkFace_None;
    dkColorStateDefaults(&color);
    dkColorWriteStateDefaults(&write);
    dkCmdBufBindRasterizerState(cmd, &raster);
    dkCmdBufBindColorState(cmd, &color);
    dkCmdBufBindColorWriteState(cmd, &write);
    dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
    lists[i] = dkCmdBufFinishList(cmd);
  }
  DkQueueMaker qm;
  dkQueueMakerDefaults(&qm, device);
  qm.flags = DkQueueFlags_Graphics;
  DkQueue queue = dkQueueCreate(&qm);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  uint64_t frames = 0;
  const uint64_t started = armGetSystemTick();
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    if (dkQueueIsInErrorState(queue)) Fail("GPU queue entered error state");
    const int slot = dkQueueAcquireImage(queue, swapchain);
    if (slot < 0 || slot >= int(kFrames)) Fail("Invalid acquired image");
    dkQueueSubmitCommands(queue, lists[slot]);
    dkQueuePresentImage(queue, swapchain, slot);
    if (++frames % 600 == 0 && log_file) {
      std::fprintf(log_file, "frames=%llu elapsed=%.3f queue_error=%d\n",
          static_cast<unsigned long long>(frames),
          double(armGetSystemTick() - started) / double(armGetSystemTickFreq()),
          int(dkQueueIsInErrorState(queue)));
      std::fflush(log_file);
    }
  }
  dkQueueWaitIdle(queue);
  dkQueueDestroy(queue);
  dkCmdBufDestroy(cmd);
  dkMemBlockDestroy(commands);
  dkMemBlockDestroy(code);
  dkSwapchainDestroy(swapchain);
  dkMemBlockDestroy(image_memory);
  dkDeviceDestroy(device);
  if (log_file) {
    std::fprintf(log_file, "PASS clean shutdown after %llu frames\n", static_cast<unsigned long long>(frames));
    std::fclose(log_file);
  }
  return 0;
}
