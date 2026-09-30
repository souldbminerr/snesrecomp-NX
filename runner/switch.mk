# switch.mk — SNESRecomp Switch (libnx) source fragment for GNU make.
#
# A game Makefile includes this AFTER setting SNESRECOMP_ROOT:
#   SNESRECOMP_ROOT ?= ../snesrecomp
#   include $(SNESRECOMP_ROOT)/runner/switch.mk
#
# Provides:
#   SNESRECOMP_SWITCH_CFILES   explicit runner .c list (mirrors the core of
#                              runner/runner.cmake; desktop dev-only files out)
#   SNESRECOMP_SWITCH_INCLUDES runner include dirs
#   SNESRECOMP_SWITCH_DEFS     compiler defines for a Switch build
#
# Deliberately excluded (desktop/dev-only, see docs/SWITCH.md):
#   src/debug_server.c + src/desktop/post_mortem.c (SNESRECOMP_ENABLE_TRACE)
#   src/emu_oracle_cmds.c (oracle verify backend), src/cosim*.c (SNES_COSIM),
#   src/mod_runtime.cpp + src/snes_text_xlate.cpp (SNESRECOMP_ENABLE_MODS),
#   src/benchmark.c (host tool), src/desktop/glsl_shader.c (desktop GL).
# With SNESRECOMP_TRACE unset, debug_server.h degrades every debug_server_*
# call site to a no-op stub, so debug_server.c is not needed to link.
# recomp_post_mortem_dump (referenced only by desktop crash handlers that
# are compiled out on __SWITCH__) is covered by a weak stub in
# src/switch/switch_impl.c.
#
# SDL backend is forced to SDL2: switch portlibs ships SDL 2.28.x only,
# and on Switch SDL_RENDERER_ACCELERATED is GLES2 — the shared SDL
# texture presenter IS the OpenGL path, no custom GL needed.

# Neutralize SDL2's Switch keyboard pump (see __wrap_swkbdInlineUpdate in
# src/switch/switch_impl.c). Game Makefiles append this to LDFLAGS.
SNESRECOMP_SWITCH_WRAP := -Wl,--wrap,swkbdInlineUpdate

SNESRECOMP_SWITCH_ROOT := $(dir $(lastword $(MAKEFILE_LIST)))

SNESRECOMP_SWITCH_CFILES := \
	$(SNESRECOMP_SWITCH_ROOT)src/common_cpu_infra.c \
	$(SNESRECOMP_SWITCH_ROOT)src/common_rtl.c \
	$(SNESRECOMP_SWITCH_ROOT)src/widescreen.c \
	$(SNESRECOMP_SWITCH_ROOT)src/recomp_hw.c \
	$(SNESRECOMP_SWITCH_ROOT)src/host_paths.c \
	$(SNESRECOMP_SWITCH_ROOT)src/launcher.c \
	$(SNESRECOMP_SWITCH_ROOT)src/launcher_cache.c \
	$(SNESRECOMP_SWITCH_ROOT)src/launcher_picker.c \
	$(SNESRECOMP_SWITCH_ROOT)src/rom_image_verify.c \
	$(SNESRECOMP_SWITCH_ROOT)src/crc32.c \
	$(SNESRECOMP_SWITCH_ROOT)src/sha256.c \
	$(SNESRECOMP_SWITCH_ROOT)src/keybinds.c \
	$(SNESRECOMP_SWITCH_ROOT)src/cpu_state.c \
	$(SNESRECOMP_SWITCH_ROOT)src/cpu_trace.c \
	$(SNESRECOMP_SWITCH_ROOT)src/audio_trace.c \
	$(SNESRECOMP_SWITCH_ROOT)src/ppu_dma_trace.c \
	$(SNESRECOMP_SWITCH_ROOT)src/host_report.c \
	$(SNESRECOMP_SWITCH_ROOT)src/execution_mode.c \
	$(SNESRECOMP_SWITCH_ROOT)src/mod_audio.c \
	$(SNESRECOMP_SWITCH_ROOT)src/host_mesh.c \
	$(SNESRECOMP_SWITCH_ROOT)src/host_mesh_builder.c \
	$(SNESRECOMP_SWITCH_ROOT)src/guarded_patch.c \
	$(SNESRECOMP_SWITCH_ROOT)src/util.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/apu.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/cart.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/cpu.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/dma.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/dsp.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/dsp1.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/dsp1_hle.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/joypad.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/audio_shadow.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/dsp_shadow.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/msu1.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/color_lut.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/ppu.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/ppu_legacy.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/sa1.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/sdd1.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/ws_shadow.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/snes.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/snes_other.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/spc.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/superfx.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/cx4.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/interp816.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/tier2_capture.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/interp_bridge.c \
	$(SNESRECOMP_SWITCH_ROOT)src/desktop/mmx_config.c \
	$(SNESRECOMP_SWITCH_ROOT)src/switch/switch_impl.c

SNESRECOMP_SWITCH_INCLUDES := \
	$(SNESRECOMP_SWITCH_ROOT)src \
	$(SNESRECOMP_SWITCH_ROOT)src/snes \
	$(SNESRECOMP_SWITCH_ROOT)src/switch \
	$(SNESRECOMP_SWITCH_ROOT)src/desktop

# SNESRECOMP_SDL3=0 selects the SDL2 API throughout sdl_compat.h and the
# host shell (pull audio callback, controller API) — the only SDL the
# switch portlibs provide. SNESRECOMP_TRACE stays 0 (production).
SNESRECOMP_SWITCH_DEFS := \
	-D__SWITCH__ \
	-DSNESRECOMP_SDL3=0 \
	-DSNESRECOMP_TRACE=0 \
	-DSNESRECOMP_ENABLE_MODS=0
