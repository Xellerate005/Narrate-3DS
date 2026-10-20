# Narrate - M4B audiobook player for the Nintendo 3DS
#
#   1. ./setup_deps.sh          (fetches the FAAD2 AAC decoder into deps/faad2)
#   2. make                     (produces narrate.3dsx)
#   3. make cia                 (optional: produces narrate.cia; needs makerom + bannertool on your PATH, see README)
#
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/3ds_rules
export PATH := $(DEVKITPRO)/tools/bin:$(PATH)

TARGET      := narrate
BUILD       := build
SRC_DIR     := source
FAAD        ?= deps/faad2

APP_TITLE   := Narrate
APP_DESC    := M4B audiobook player
APP_AUTHOR  := homebrew
ICON        := icon.png

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(wildcard $(FAAD)/libfaad/decoder.c),)
$(error FAAD2 sources not found in '$(FAAD)'. Run ./setup_deps.sh first (see README.md))
endif
endif

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ -std=gnu11

# FAAD2 build options. FIXED_POINT is much faster on the 3DS's ARM11 than floating point.
# If you ever hear garbled audio, try removing -DFIXED_POINT.
FAAD_DEFS := -DPACKAGE_VERSION=\"2.10.1\" -DFIXED_POINT -DSBR_DEC -DPS_DEC -DHAVE_STDINT_H -DHAVE_INTTYPES_H -DHAVE_STRING_H -DHAVE_STRCHR -DHAVE_MEMCPY -DNDEBUG
FAAD_FLAGS := -O2 -g -mword-relocations -fno-strict-aliasing $(ARCH) -ffunction-sections -fdata-sections -w \
              -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types

LIBDIRS  := $(PORTLIBS) $(CTRULIB)
INCLUDE  := $(foreach d,$(LIBDIRS),-I$(d)/include) -I$(FAAD)/include
LIBPATHS := $(foreach d,$(LIBDIRS),-L$(d)/lib)
LIBS     := -lcitro2d -lcitro3d -lpng -ljpeg -lz -lctru -lm
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map

SRCS      := $(wildcard $(SRC_DIR)/*.c)
FAAD_SRCS := $(wildcard $(FAAD)/libfaad/*.c)
OBJS      := $(SRCS:$(SRC_DIR)/%.c=$(BUILD)/%.o) $(FAAD_SRCS:$(FAAD)/libfaad/%.c=$(BUILD)/faad/%.o)

.PHONY: all clean cia cia-tools
all: $(TARGET).3dsx

$(BUILD) $(BUILD)/faad:
	@mkdir -p $@

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) $(INCLUDE) -c $< -o $@

# FAAD2's common.h re-declares strchr/strrchr K&R-style, which clashes with newlib. Drop that line once.
FAAD_PATCH := $(FAAD)/.narrate_patched
$(FAAD_PATCH):
	@sed -i '/char \*strchr(), \*strrchr();/d' $(FAAD)/libfaad/common.h
	@touch $@

$(BUILD)/faad/%.o: $(FAAD)/libfaad/%.c $(FAAD_PATCH) | $(BUILD)/faad
	@echo faad2/$(notdir $<)
	@$(CC) -MMD -MP $(FAAD_FLAGS) $(FAAD_DEFS) $(INCLUDE) -I$(FAAD)/libfaad -c $< -o $@

$(TARGET).elf: $(OBJS)
	@echo linking $(notdir $@)
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBPATHS) $(LIBS) -o $@

$(TARGET).smdh: $(ICON) Makefile
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	@3dsxtool $< $@ --smdh=$(TARGET).smdh
	@echo built ... $(notdir $@)

# ---- CIA (installable title) -------------------------------------------------------------------
cia: $(TARGET).cia

cia-tools:
	@command -v makerom >/dev/null 2>&1 || { echo "makerom not found on PATH - see 'Building a CIA' in README.md"; exit 1; }
	@command -v bannertool >/dev/null 2>&1 || { echo "bannertool not found on PATH - see 'Building a CIA' in README.md"; exit 1; }

banner.bnr: cia/banner.png cia/banner.wav | cia-tools
	@bannertool makebanner -i cia/banner.png -a cia/banner.wav -o $@

$(TARGET).cia: $(TARGET).elf $(TARGET).smdh banner.bnr cia/narrate.rsf | cia-tools
	@makerom -f cia -o $@ -elf $(TARGET).elf -rsf cia/narrate.rsf -icon $(TARGET).smdh -banner banner.bnr -exefslogo -target t
	@echo built ... $(notdir $@)

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia banner.bnr

-include $(OBJS:.o=.d)
