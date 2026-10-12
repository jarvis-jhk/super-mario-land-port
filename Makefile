# Super Mario Land port - build file. See docs/BUILDING.md.
CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
BUILD   := build
VERSION ?= M0-dev
VERSION_FLAGS := -DSML_VERSION='"$(VERSION)"'

HW_SRC  := src/hw/machine.c src/hw/lcd.c src/hw/apu.c
EMU_SRC := verify/emu/sm83.c verify/emu/gb.c
COMMON  := verify/common/png.c src/util/movie.c
GAME_SRC := $(wildcard src/game/*.c)
FNTEST_REG := $(wildcard verify/fntest/registry*.c)

all: $(BUILD)/emurun $(BUILD)/testrom

$(BUILD)/emurun: verify/tools/emurun.c $(HW_SRC) $(EMU_SRC) $(COMMON) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/testrom: verify/tests/testrom.c $(HW_SRC) $(EMU_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD):
	mkdir -p $@

# --- Gambatte oracle (GPLv2, fetched by tools/fetch_reference.sh) ---------
GAMBATTE := reference/gambatte/libgambatte
GB_SRCS  := $(wildcard $(GAMBATTE)/src/*.cpp $(GAMBATTE)/src/mem/*.cpp \
              $(GAMBATTE)/src/mem/mbc/*.cpp $(GAMBATTE)/src/mem/snes_spc/*.cpp \
              $(GAMBATTE)/src/sound/*.cpp $(GAMBATTE)/src/video/*.cpp) \
            $(GAMBATTE)/src/file/file.cpp $(GAMBATTE)/src/file/crc32.cpp
GB_OBJS  := $(patsubst $(GAMBATTE)/%.cpp,$(BUILD)/gambatte/%.o,$(GB_SRCS))
GB_FLAGS := -O2 -std=c++11 -fno-exceptions -fno-rtti -DHAVE_STDINT_H -DREVISION=0 \
            -I$(GAMBATTE)/src -I$(GAMBATTE)/include -I$(GAMBATTE)/../common -w

$(BUILD)/gambatte/%.o: $(GAMBATTE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GB_FLAGS) -c $< -o $@

$(BUILD)/libgambatte.a: $(GB_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/%.o: verify/common/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/movie.o: src/util/movie.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gbref: verify/gambatte/gbref.cpp $(BUILD)/png.o $(BUILD)/movie.o $(BUILD)/libgambatte.a
	$(CXX) $(GB_FLAGS) -o $@ $^

oracle: $(BUILD)/gbref

# Blargg's CPU test ROMs (downloaded by tools/fetch_testroms.sh)
TESTROMS := tools/testroms/gb-test-roms-master
check-emu: $(BUILD)/testrom
	$(BUILD)/testrom $(TESTROMS)/cpu_instrs/cpu_instrs.gb 6000
	$(BUILD)/testrom $(TESTROMS)/instr_timing/instr_timing.gb 600
	$(BUILD)/testrom $(TESTROMS)/mem_timing/mem_timing.gb 600

clean:
	rm -rf $(BUILD)

.PHONY: all clean check-emu oracle

# === platform toolchain test (Zig cross-compiler + SDL2) ==================
# Needs tools/fetch_toolchain.sh to have run. All caches stay inside the
# project (tools/build_env.sh). Outputs: build/<target>/sdl_hello[.exe]
ZIGENV  := SML_ROOT="$(CURDIR)"; . tools/build_env.sh &&
ZIGCC   := $$ZIG cc -O2 -Wall -Wextra -Itools/sdl2/include
HELLO   := src/platform/sdl_hello.c

.PHONY: toolchain hello-all hello-linux-x86_64 hello-linux-aarch64 hello-windows-x86_64

toolchain:
	tools/fetch_toolchain.sh

hello-all: hello-linux-x86_64 hello-linux-aarch64 hello-windows-x86_64

# The glibc suffix is the minimum glibc the binary needs at run time.
# --allow-shlib-undefined: the Ubuntu 24.04 libSDL2 we link against references
# newer glibc symbols than the 2.31 we target; they exist on the real system.
hello-linux-x86_64: $(BUILD)/linux-x86_64/sdl_hello
$(BUILD)/linux-x86_64/sdl_hello: $(HELLO)
	@mkdir -p $(dir $@)
	$(ZIGENV) $(ZIGCC) -target x86_64-linux-gnu.2.31 $< -o $@ \
	    -Ltools/sdl2/linux-x86_64 -lSDL2 -Wl,--allow-shlib-undefined

hello-linux-aarch64: $(BUILD)/linux-aarch64/sdl_hello
$(BUILD)/linux-aarch64/sdl_hello: $(HELLO)
	@mkdir -p $(dir $@)
	$(ZIGENV) $(ZIGCC) -target aarch64-linux-gnu.2.31 $< -o $@ \
	    -Ltools/sdl2/linux-arm64 -lSDL2 -Wl,--allow-shlib-undefined

hello-windows-x86_64: $(BUILD)/windows-x86_64/sdl_hello.exe
$(BUILD)/windows-x86_64/sdl_hello.exe: $(HELLO)
	@mkdir -p $(dir $@)
	$(ZIGENV) $(ZIGCC) -target x86_64-windows-gnu $< -o $@ \
	    tools/sdl2/mingw/x86_64-w64-mingw32/lib/libSDL2.dll.a
	cp tools/sdl2/mingw/x86_64-w64-mingw32/bin/SDL2.dll $(dir $@)

# === the port ===============================================================
# `make sml` builds build/sml with the host compiler (needs the SDL2 headers
# and link stub from tools/fetch_toolchain.sh; links the system libSDL2).
# `make port-<target>` builds release binaries with Zig for linux-x86_64,
# linux-aarch64, windows-x86_64; `make appimage-<arch>` packs the Linux ones.
# The port: translated game + timing shadow + hardware model. No interpreter.
CORE_SRC := src/core/core.c src/core/rom_id.c src/core/shadow.c src/core/rt_shadow.c src/core/access_table.c \
            $(GAME_SRC) src/util/sha1.c
PORT_SRC := src/platform/fault.c src/platform/sdl_main.c src/platform/settings.c src/platform/audio_sync.c $(CORE_SRC) $(HW_SRC) src/util/movie.c verify/common/png.c

$(BUILD)/sml: $(PORT_SRC) $(wildcard src/*/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(VERSION_FLAGS) -Itools/sdl2/include -o $@ $(PORT_SRC) \
	    -Ltools/sdl2/linux-x86_64 -lSDL2 -Wl,-rpath-link,tools/sdl2/linux-x86_64

sml: $(BUILD)/sml

# Linux release binaries link a static SDL2 built from source for glibc 2.31
# (tools/build_sdl2.sh); SDL loads X11, PulseAudio/ALSA and OpenGL at run
# time, so the binaries need no SDL2 package on the target system.
$(BUILD)/sdl2-%/lib/libSDL2.a:
	tools/build_sdl2.sh $*

port-linux-x86_64: $(PORT_SRC) $(BUILD)/sdl2-x86_64/lib/libSDL2.a
	@mkdir -p $(BUILD)/linux-x86_64
	$(ZIGENV) $$ZIG cc -O2 -Wall -Wextra $(VERSION_FLAGS) -target x86_64-linux-gnu.2.31 -I$(BUILD)/sdl2-x86_64/include/SDL2 \
	    $(PORT_SRC) -o $(BUILD)/linux-x86_64/sml $(BUILD)/sdl2-x86_64/lib/libSDL2.a -lm -ldl -lpthread -lrt

port-linux-aarch64: $(PORT_SRC) $(BUILD)/sdl2-aarch64/lib/libSDL2.a
	@mkdir -p $(BUILD)/linux-aarch64
	$(ZIGENV) $$ZIG cc -O2 -Wall -Wextra $(VERSION_FLAGS) -target aarch64-linux-gnu.2.31 -I$(BUILD)/sdl2-aarch64/include/SDL2 \
	    $(PORT_SRC) -o $(BUILD)/linux-aarch64/sml $(BUILD)/sdl2-aarch64/lib/libSDL2.a -lm -ldl -lpthread -lrt

port-windows-x86_64: $(PORT_SRC)
	@mkdir -p $(BUILD)/windows-x86_64
	$(ZIGENV) $(ZIGCC) $(VERSION_FLAGS) -target x86_64-windows-gnu $(PORT_SRC) -o $(BUILD)/windows-x86_64/sml.exe \
	    tools/sdl2/mingw/x86_64-w64-mingw32/lib/libSDL2.dll.a
	cp tools/sdl2/mingw/x86_64-w64-mingw32/bin/SDL2.dll $(BUILD)/windows-x86_64/

port-all: port-linux-x86_64 port-linux-aarch64 port-windows-x86_64

# AppImages: build/dist/Super_Mario_Land_Port-<arch>.AppImage
appimage-x86_64: port-linux-x86_64
	packaging/linux/make_appimage.sh x86_64
appimage-aarch64: port-linux-aarch64
	packaging/linux/make_appimage.sh aarch64
appimage-all: appimage-x86_64 appimage-aarch64

.PHONY: sml port-all port-linux-x86_64 port-linux-aarch64 port-windows-x86_64
.PHONY: appimage-x86_64 appimage-aarch64 appimage-all

# Frontend unit tests (settings file, audio rate control) and a headless run
# of the SDL frontend: no window, audio device or settings file needed.
$(BUILD)/frontend_test: verify/tests/frontend_test.c src/platform/settings.c src/platform/audio_sync.c src/platform/*.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ verify/tests/frontend_test.c src/platform/settings.c src/platform/audio_sync.c -lm

FRONTEND_BINARY ?= $(BUILD)/sml
check-frontend: $(BUILD)/frontend_test $(FRONTEND_BINARY)
	$(BUILD)/frontend_test
	$(FRONTEND_BINARY) --rom rom/sml.gb --headless 600 | grep -qx 'frames=600 polls=[0-9]*'

.PHONY: check-frontend

# === function tests (verify/fntest) ========================================
# fntest is built against a snapshot of the hardware model and CPU taken
# from the last commit (build/stable/), so that work in progress on src/hw
# or verify/emu does not break function testing. `make stable-hw` refreshes it.
STABLE     := $(BUILD)/stable
# The reference snapshot is copied from this audited tree (no private history).
STABLE_SRC := $(STABLE)/src/hw/machine.c $(STABLE)/src/hw/lcd.c $(STABLE)/src/hw/apu.c \
              $(STABLE)/verify/emu/sm83.c $(STABLE)/verify/emu/gb.c

stable-hw:
	rm -rf $(STABLE) && mkdir -p $(STABLE)
	cp -R src $(STABLE)/
	mkdir -p $(STABLE)/verify
	cp -R verify/emu $(STABLE)/verify/

$(STABLE)/src/hw/machine.c:
	$(MAKE) stable-hw

$(BUILD)/fntest: verify/fntest/fntest.c $(FNTEST_REG) $(GAME_SRC) $(STABLE)/src/hw/machine.c src/util/movie.c $(wildcard src/game/*.h verify/fntest/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -I$(STABLE)/src/hw -include $(STABLE)/src/hw/machine.h -include $(STABLE)/verify/emu/gb.h -o $@ verify/fntest/fntest.c $(FNTEST_REG) $(GAME_SRC) $(STABLE_SRC) src/util/movie.c src/core/access_table.c

fntest: $(BUILD)/fntest
	$(BUILD)/fntest rom/sml.gb --model agb --movie build/resync11.movie

.PHONY: fntest stable-hw

# === port runners and stable reference runner (lockstep checks) =============
# portrun: the port's own core (pure C, no interpreter) on the stable hardware
# model. portrun_hybrid: development core, interpreter + translated routines
# (verify/tools/core_hybrid.c), for bisecting with SML_HYBRID_ONLY/SML_C_MAIN.
STABLE_INC  := -I$(STABLE)/src/hw -include $(STABLE)/src/hw/machine.h -include $(STABLE)/verify/emu/gb.h
STABLE_HW   := $(STABLE)/src/hw/machine.c $(STABLE)/src/hw/lcd.c $(STABLE)/src/hw/apu.c
PORTRUN_SRC := verify/tools/portrun.c $(CORE_SRC) src/util/movie.c
HYBRID_SRC  := verify/tools/portrun.c verify/tools/core_hybrid.c verify/tools/hybrid.c src/core/rom_id.c \
               src/core/shadow.c src/core/rt_shadow.c src/core/access_table.c $(GAME_SRC) $(FNTEST_REG) \
               src/util/movie.c src/util/sha1.c

$(BUILD)/portrun: $(PORTRUN_SRC) $(STABLE)/src/hw/machine.c $(wildcard src/*/*.h)
	$(CC) $(CFLAGS) -I$(STABLE)/src/hw -include $(STABLE)/src/hw/machine.h -o $@ $(PORTRUN_SRC) $(STABLE_HW)

$(BUILD)/portrun_hybrid: $(HYBRID_SRC) $(STABLE)/src/hw/machine.c $(wildcard src/*/*.h verify/tools/*.h)
	$(CC) $(CFLAGS) $(STABLE_INC) -o $@ $(HYBRID_SRC) $(STABLE_SRC)

$(BUILD)/emurun_s: verify/tools/emurun.c $(STABLE)/src/hw/machine.c verify/common/png.c src/util/movie.c
	$(CC) $(CFLAGS) $(STABLE_INC) -o $@ verify/tools/emurun.c $(STABLE_SRC) verify/common/png.c src/util/movie.c

$(BUILD)/shadowtest: verify/tools/shadowtest.c src/core/shadow.c src/core/access_table.c $(STABLE)/src/hw/machine.c
	$(CC) $(CFLAGS) $(STABLE_INC) -o $@ verify/tools/shadowtest.c src/core/shadow.c src/core/access_table.c $(STABLE_SRC) src/util/movie.c

# Full verification (needs build/resync11.movie): function tests, the timing
# shadow, and lockstep of the port (pure C) and of the hybrid development core
# against the original on the console-verified TAS.
check: $(BUILD)/fntest $(BUILD)/portrun $(BUILD)/portrun_hybrid $(BUILD)/emurun_s $(BUILD)/shadowtest
	$(BUILD)/fntest rom/sml.gb --model agb --movie build/resync11.movie --samples 100 --fuzz 8 | tail -1
	$(BUILD)/shadowtest rom/sml.gb --model agb --movie build/resync11.movie --frames 46000 | tail -1
	$(BUILD)/emurun_s rom/sml.gb --model agb --movie build/resync11.movie --poll-log $(BUILD)/e_agb.log
	$(BUILD)/portrun rom/sml.gb --model agb --movie build/resync11.movie --poll-log $(BUILD)/p_agb.log
	python3 tools/compare_polls.py $(BUILD)/e_agb.log $(BUILD)/p_agb.log
	env -u SML_C_MAIN -u SML_HYBRID_ONLY $(BUILD)/portrun_hybrid rom/sml.gb --model agb --movie build/resync11.movie --poll-log $(BUILD)/h_agb.log
	python3 tools/compare_polls.py $(BUILD)/e_agb.log $(BUILD)/h_agb.log

# Rare states plus the complete ending and pause/reset control flow, for the
# port and for the hybrid core.
check-states: $(BUILD)/fntest $(BUILD)/portrun $(BUILD)/portrun_hybrid $(BUILD)/emurun_s
	$(BUILD)/fntest rom/sml.gb --model agb --movie build/resync11.movie \
	    --polls 100 --samples 0 --fuzz 8 --cases verify/fntest/state-cases.txt | tail -1
	@for movie in ending game-over pause-reset; do \
	    $(BUILD)/emurun_s rom/sml.gb --model agb --movie verify/movies/$$movie.movie --poll-log $(BUILD)/states_$$movie.reference.log && \
	    $(BUILD)/portrun rom/sml.gb --model agb --movie verify/movies/$$movie.movie --poll-log $(BUILD)/states_$$movie.port.log && \
	    python3 tools/compare_polls.py $(BUILD)/states_$$movie.reference.log $(BUILD)/states_$$movie.port.log || exit $$?; \
	    env -u SML_C_MAIN -u SML_HYBRID_ONLY $(BUILD)/portrun_hybrid rom/sml.gb --model agb --movie verify/movies/$$movie.movie --poll-log $(BUILD)/states_$$movie.hybrid.log && \
	    python3 tools/compare_polls.py $(BUILD)/states_$$movie.reference.log $(BUILD)/states_$$movie.hybrid.log || exit $$?; \
	done

# The port on the other console models (CGB and DMG timing).
check-models: $(BUILD)/portrun $(BUILD)/emurun_s
	@for model in cgb dmg; do \
	    $(BUILD)/emurun_s rom/sml.gb --model $$model --movie build/resync11.movie --poll-log $(BUILD)/e_$$model.log && \
	    $(BUILD)/portrun rom/sml.gb --model $$model --movie build/resync11.movie --poll-log $(BUILD)/p_$$model.log && \
	    python3 tools/compare_polls.py $(BUILD)/e_$$model.log $(BUILD)/p_$$model.log || exit $$?; \
	done

.PHONY: check check-states check-models

# Restricted execution metadata is rebuilt locally, never committed.
# emurun_s does not depend on the access table, so this bootstraps from zero.
src/core/access_table.c: $(BUILD)/emurun_s $(BUILD)/resync11.movie tools/regen_access_table.py
	python3 tools/regen_access_table.py --jobs 6

$(BUILD)/resync11.movie: $(BUILD)/gbref reference/tas/SML1.1Resync.bk2
	python3 tools/tas_convert.py reference/tas/SML1.1Resync.bk2 $(BUILD)/resync11.fin
	$(BUILD)/gbref rom/sml.gb --model agb --frame-input $(BUILD)/resync11.fin 176 --record-polls $@

check-fault:
	python3 verify/tests/fault_test.py
.PHONY: check-fault
