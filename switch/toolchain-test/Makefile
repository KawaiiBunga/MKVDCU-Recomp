ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO to the devkitPro root, e.g. C:/devkitPro)
endif

# This avoids devkitPro's recursive make template because the workspace path
# contains spaces on Windows. All target names stay relative to this directory.
TARGET := mkvdcu-switch-toolchain-test
BUILD := build
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++.exe
STRIP := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-strip.exe
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro.exe
NACPTOOL := $(DEVKITPRO)/tools/bin/nacptool.exe
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := -std=gnu++23 -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ -Isource -I$(DEVKITPRO)/libnx/include
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -Wl,--gc-sections $(ARCH) -L$(DEVKITPRO)/libnx/lib -lnx

.PHONY: all clean
all: $(TARGET).nro

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/main.o: source/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(BUILD)/main.o
	$(CXX) $^ -o $@ $(LDFLAGS)

$(TARGET).nacp:
	$(NACPTOOL) --create "MKVDCU Switch toolchain test" "MKVDCU-Recomp" "0.1.0" $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	$(STRIP) --strip-all -o $(BUILD)/$(TARGET).stripped $(TARGET).elf
	$(ELF2NRO) $(BUILD)/$(TARGET).stripped $@ --nacp=$(TARGET).nacp

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

-include $(BUILD)/main.d
