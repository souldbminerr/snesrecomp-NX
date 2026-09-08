/*
 * switch_impl.h — Nintendo Switch (libnx) platform layer for SNESRecomp.
 *
 * Implemented in switch_impl.c. Only compiled into Switch builds
 * (__SWITCH__, devkitA64 + switch portlibs SDL2). Desktop builds never
 * see this file.
 *
 * Design notes:
 * - Video/audio/input all go through SDL2 (portlibs 2.28.x). On Switch,
 *   SDL_CreateRenderer(..., SDL_RENDERER_ACCELERATED) yields an OpenGL
 *   ES2 renderer, so the shared SDL texture-streaming presenter in
 *   desktop/mmx23_host_main.inc IS the OpenGL path here — no custom GL
 *   code is needed and no devkitPro SDL sources are modified.
 * - The ROM lives on the SD card next to the homebrew, never in the
 *   binary: sdmc:/switch/<SWITCH_APP_DIR>/rom.sfc (or rom.smc).
 *   SWITCH_APP_DIR is set by the game Makefile (default: the game
 *   target name, e.g. dkc2). romfs:/rom.sfc is probed as a fallback so
 *   developers can also bake a test ROM into RomFS.
 * - libnx services used: romfs, sdmc fleet via newlib (mkdir/chdir/
 *   fopen), appletMainLoop for Home-button/suspend handling. No socket
 *   init: the TCP debug server, co-sim and oracle backends are desktop
 *   dev tools and are compiled out of Switch builds.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Early platform bring-up, called first in main() (before argv handling):
 * romfsInit(), creates sdmc:/switch/<app>/ if needed and chdirs there so
 * every relative path below (config.ini, keybinds.ini, saves/, ROM probe)
 * lands on the SD card. Also enables debug.log file logging when
 * log.flag is present (see SwitchImpl_MaybeEnableFileLog). Safe to call
 * twice. */
void SwitchImpl_Init(void);

/* Late teardown, called right before SDL_Quit(): romfsExit(). */
void SwitchImpl_Exit(void);

/* Flag-gated SD logging, also called from SwitchImpl_Init: when
 * log.flag exists in the app dir, stderr is redirected (unbuffered) to
 * debug.log there. Exposed so hosts can re-arm it after their own
 * chdir if needed. */
void SwitchImpl_MaybeEnableFileLog(void);
/* Per-frame applet tick. Returns 0 when the applet must exit (Home-button
 * exit request / suspend denied). The caller stops the main loop. Must be
 * called every frame; libnx requires regular appletMainLoop() pumps or the
 * OS kills the title. */
int SwitchImpl_Tick(void);

/* Resolve the SNES ROM into `out` (size `cap`). Probe order:
 *   1. `positional` argv path, when non-NULL and readable.
 *   2. <cwd>/rom.sfc, <cwd>/rom.smc  (cwd is sdmc:/switch/<app>/)
 *   3. romfs:/rom.sfc, romfs:/rom.smc (dev/CI fallback)
 * Returns 1 on success, 0 when nothing was found (caller then shows
 * ThrowMissingROM). */
int SwitchImpl_ResolveRom(char *out, size_t cap, const char *positional);

/* Fatal ROM-missing path: persists MISSING_ROM.txt beside the binary,
 * prints placement instructions, and exits. SDL may already be
 * initialized (the host calls this after SDL_Init); the message box is
 * best-effort on Switch and stderr/nxlink always gets the text. */
void ThrowMissingROM(void);

/* Name of the SD app directory (SWITCH_APP_DIR, e.g. "dkc2").
 * sdmc:/switch/<name>/ holds rom.sfc, config.ini, keybinds.ini, saves/. */
const char *SwitchImpl_AppDir(void);

#ifdef __cplusplus
}
#endif
