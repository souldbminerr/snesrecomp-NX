# Nintendo Switch port (libnx + SDL2 + OpenGL)

Homebrew port target for SNESRecomp games. Video/audio/input go through
the switch portlibs **SDL2** (2.28.x); on Switch,
`SDL_RENDERER_ACCELERATED` is an **OpenGL ES2** renderer, so the shared
SDL texture presenter *is* the OpenGL path — no game-side
`OpenGLRenderer_Create` is compiled or linked on `__SWITCH__`, and no
devkitPro SDL sources are modified (the DKP example patterns are vendored
into `runner/src/switch/`).

## Prerequisites

- devkitPro with devkitA64, libnx, and switch portlibs SDL2
  (`pacman -S switch-dev switch-sdl2` in msys2, or the graphical
  installer with those groups).
- `DEVKITPRO` / `DEVKITA64` set (the msys2 shell does this for you).
- A game repository (SMW / ALttP / MMX / DKC2 / …) with generated recomp
  sources — this framework repo alone has no game `main()`.

## Add it to a game repo

1. Copy `Makefile.switch` from this repo next to the desktop
   `CMakeLists.txt` and fill the `GAME_*` knobs:
   - `TARGET` / `APP_TITLE` / `APP_AUTHOR` (e.g. `TARGET := dkc2`).
   - `SWITCH_APP_DIR` — SD dir name; defaults to `$(TARGET)`.
   - `GAME_CFILES` — generated `src/gen/*.c`, the host wrapper `.c`
     that defines the `MMX_*` macros and `#include`s
     `desktop/mmx23_host_main.inc`, plus game integration files.
     Basenames must be unique across game + runner files (checked).
   - `GAME_INCLUDES` — header dirs (`MMX_RTL_HEADER`, display, SPC…).
   - `GAME_DEFS` — extra `-D` flags if the game needs them.
2. Build: `make -f Makefile.switch` → `<target>.nro` (+ `.nacp`).
3. Copy `<target>.nro` to `sdmc:/switch/<app>/` and place the ROM at
   `sdmc:/switch/<app>/rom.sfc` (`.smc` copier-header dumps accepted).

## Runtime layout (`sdmc:/switch/dkc2/` as example)

| Path | Purpose |
|---|---|
| `dkc2.nro` | homebrew binary |
| `rom.sfc` / `rom.smc` | game ROM (never shipped; user-supplied) |
| `config.ini` | created with defaults on first boot; editable |
| `keybinds.ini` | controller/keyboard bindings |
| `saves/` | SRAM + savestates |
| `MISSING_ROM.txt` | written when no ROM was found (explains placement) |

ROM probe order: positional argv → `./rom.sfc`, `./rom.smc`,
`./game.sfc`, `./game.smc` (cwd is the SD app dir) → `romfs:/rom.sfc`,
`romfs:/rom.smc` (dev fallback for baked-in CI ROMs).

## Controls

Joy-Cons / Pro Controller via SDL's built-in Switch mappings
(GameController API with raw-joystick fallback, same as desktop).
Keyboard bindings are inert (no keyboard on Horizon). Home-button /
suspend is pumped every frame (`appletMainLoop`); an applet exit request
also arrives as `SDL_QUIT`.

## Fixed differences from desktop

- Window is a fixed 720p SDL window (1280�--720, upscaled by GLES2);
  window-scale / fullscreen / borderless / mouse / hit-test paths are
  compiled out.
- No GUI launcher or file picker: the game boots straight into the ROM.
- Audio device failure is non-fatal (runs silent) instead of aborting.
- No `signal()`/`atexit` crash handlers (Horizon has no POSIX signals
  path here); breadcrumbs still go to stderr (nxlink USB log).
- 720p + fixed 60 Hz vsync pacing stays on (`DisableFrameDelay`
  equivalent is not exposed; edit `switch_impl`/config if needed).

## Deliberately excluded (desktop dev tools)

TCP debug server, co-simulation, snes9x oracle, `.snesmod` packages,
GLSL shader backend, post-mortem minidumps. `SNESRECOMP_TRACE` is forced
to 0, which turns every `debug_server_*` call site into a compiled-out
stub via `debug_server.h`.

## Files

| Path | Role |
|---|---|
| `runner/src/switch/switch_impl.h/.c` | romfs + SD app-dir bring-up, applet tick, SD ROM resolver, missing-ROM fatal |
| `runner/switch.mk` | make fragment: runner source list, includes, Switch defines (mirrors `runner.cmake`) |
| `Makefile.switch` | copy-into-game template Makefile (devkitA64, `sdl2-config` flags, `.nro` packaging) |
| `runner/src/desktop/mmx23_host_main.inc` | shared host shell with `__SWITCH__` guards (window/audio/launcher/input) |
