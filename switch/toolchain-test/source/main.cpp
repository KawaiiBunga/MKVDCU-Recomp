#include <cstdio>
#include <switch.h>

int main(int argc, char* argv[]) {
  consoleInit(nullptr);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeAny(&pad);
  printf("MKVDCU-Recomp Switch toolchain test\n");
  printf("libnx initialized; no ReXGlue runtime is linked.\n");
  printf("Press PLUS to exit.\n");
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(nullptr);
  }
  consoleExit(nullptr);
  return 0;
}
