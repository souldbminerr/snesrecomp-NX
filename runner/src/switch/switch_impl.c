/*
 * switch_impl.c — Nintendo Switch (libnx) platform layer for SNESRecomp.
 *
 * SDL2-on-Switch patterns below (romfsInit + chdir("romfs:/") idiom,
 * appletMainLoop() in the frame loop, SDL_WINDOW_SHOWN 1280x720 window,
 * SDL_RENDERER_ACCELERATED presenter, joystick button indices) are adapted
 * from the devkitPro switch examples (graphics/sdl2/sdl2-demo,
 * graphics/sdl2/sdl2-simple). They are COPIED here so this port builds
 * against an unmodified devkitPro install — nothing under
 * $DEVKITPRO/portlibs/switch is edited.
 *
 * Only compiled when __SWITCH__ is defined (devkitA64). Desktop builds
 * never compile this file.
 */
#ifdef __SWITCH__

#include "switch_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include "common_rtl.h"

/* framedump.c is excluded from Switch builds (per-frame WRAM dumper for
 * desktop forensics); common_rtl.c still reads this global, so provide
 * the permanently-NULL instance here. No frames are ever dumped. */
#include "framedump.h"
FrameDumpCallback g_framedump_callback = 0;

#ifndef SWITCH_APP_DIR
#define SWITCH_APP_DIR "snesrecomp"
#endif

/* Weak no-op shim: desktop builds serialize a post-mortem report through
 * desktop/post_mortem.c, which Switch builds deliberately omit (no dbghelp
 * / minidump on Horizon). The host shell only references this from its
 * signal/atexit handlers, both compiled out on __SWITCH__, so this symbol
 * is purely belt-and-braces for game wrappers that call it directly. Weak
 * so a future Switch-capable post_mortem.c still wins the link. */
__attribute__((weak)) void recomp_post_mortem_dump(const char *reason, void *info) {
  (void)reason;
  (void)info;
}

const char *SwitchImpl_AppDir(void) {
  return SWITCH_APP_DIR;
}

/* SDL2's Switch keyboard driver (SDL_switchswkb.o, linked from portlibs)
 * invokes this libnx applet helper from its per-frame pump. Our titles
 * never use text input, so neutralize it at link time (--wrap,
 * switch.mk) instead of modifying devkitPro's SDL: without the wrap,
 * a stray pump with no keyboard session can enter applet code with
 * garbage context. Unused libnx swkbd objects then drop out via
 * --gc-sections. */
void __wrap_swkbdInlineUpdate(void *ctx, int reply) {
  (void)ctx;
  (void)reply;
}

static void switch_mkdir_p(const char *path) {
  char tmp[256];
  size_t len;

  if (!path || !*path) return;
  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);
  /* sdmc:/switch/<app> — create each level; EEXIST is fine. */
  for (size_t i = 1; i < len; i++) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      mkdir(tmp, 0755);
      tmp[i] = '/';
    }
  }
  mkdir(tmp, 0755);
}

static AppletHookCookie s_exit_hook;
static AppletFocusState s_last_focus = AppletFocusState_InFocus;

static void SwitchImpl_ExitHook(AppletHookType type, void *param) {
  (void)param;
  if (type == AppletHookType_OnExitRequest) {
    fprintf(stderr, "[Switch] exit requested, persisting SRAM\n");
    RtlWriteSram();
    return;
  }
  if (type == AppletHookType_OnFocusState) {
    /* Every dangerous quit path (Home Close, title takeover, sleep)
     * passes through losing foreground first, while we can still run
     * code. Save on the InFocus -> anything-else transition only, so a
     * stuck state cannot spam writes. */
    AppletFocusState now = appletGetFocusState();
    if (now != AppletFocusState_InFocus &&
        s_last_focus == AppletFocusState_InFocus) {
      fprintf(stderr, "[Switch] focus lost (%d), persisting SRAM\n",
              (int)now);
      RtlWriteSram();
    }
    s_last_focus = now;
  }
}

void SwitchImpl_Init(void) {
  static int s_inited = 0;
  char appdir[128];

  if (s_inited) return;
  s_inited = 1;

  romfsInit();

  /* Persist SRAM the moment the OS asks us to quit (Home menu Close,
   * hbmenu unload): the main loop may never get another frame to run
   * its own exit write. The hook fires on the same thread that pumps
   * appletMainLoop, between emulated frames, so the image is stable. */
  appletHook(&s_exit_hook, SwitchImpl_ExitHook, NULL);

  /* Land every relative path (config.ini, keybinds.ini, saves/, ROM
   * probe) on the SD card: sdmc:/switch/<app>/. */
  snprintf(appdir, sizeof(appdir), "sdmc:/switch/%s", SWITCH_APP_DIR);
  switch_mkdir_p("sdmc:/switch");
  switch_mkdir_p(appdir);
  if (chdir(appdir) != 0) {
    /* Non-fatal: stay wherever we are (hbmenu cwd or romfs) and let
     * the ROM probe report the miss precisely. */
    fprintf(stderr, "[Switch] chdir('%s') failed, staying in '%s'\n",
            appdir, getcwd(NULL, 0));
  } else {
    fprintf(stderr, "[Switch] data dir: %s\n", appdir);
  }
  mkdir("saves", 0755);

  SwitchImpl_MaybeEnableFileLog();
}

/* Flag-gated SD logging: when sdmc:/switch/<app>/log.flag exists,
 * stderr is redirected (unbuffered) to debug.log beside it. This is the
 * no-nxlink debug path — breadcrumbs, warnings, and fatals all use
 * stderr, so crashes remain readable straight off the SD card. stdout
 * keeps going to nxlink when connected. Truncates per boot so a crash
 * loop cannot fill the card. */
void SwitchImpl_MaybeEnableFileLog(void) {
  FILE *flag = fopen("log.flag", "rb");
  FILE *log;

  if (!flag) return;
  fclose(flag);

  log = freopen("debug.log", "w", stderr);
  if (!log) return;
  setvbuf(stderr, NULL, _IONBF, 0);
  fprintf(stderr, "[Switch] file logging: debug.log (%s)\n",
          SWITCH_APP_DIR);
}

void SwitchImpl_Exit(void) {
  appletUnhook(&s_exit_hook);
  romfsExit();
}

int SwitchImpl_Tick(void) {
  /* Home-button / suspend / exit-request pump. libnx requires this at
   * least every few seconds; every frame is the DKP example cadence.
   * The Switch SDL2 video driver also converts an applet exit request
   * into SDL_QUIT, which the host loop handles — this is the second,
   * synchronous half of that contract. */
  return appletMainLoop() ? 1 : 0;
}

static int probe_readable(const char *path, char *out, size_t cap) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  if (strlen(path) >= cap) return 0;
  strcpy(out, path);
  return 1;
}

int SwitchImpl_ResolveRom(char *out, size_t cap, const char *positional) {
  static const char *kNames[] = {
    "rom.sfc", "rom.smc", "game.sfc", "game.smc",
  };
  char cand[256];
  size_t i;

  if (!out || cap == 0) return 0;

  /* 1. Explicit positional path (nxlink / hbmenu arg forwarding). */
  if (positional && *positional && probe_readable(positional, out, cap))
    return 1;

  /* 2. SD app dir (cwd is sdmc:/switch/<app>/ after SwitchImpl_Init):
   *     sdmc:/switch/dkc2/rom.sfc   <-- the documented layout          */
  for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
    if (probe_readable(kNames[i], out, cap)) {
      fprintf(stderr, "[Switch] ROM: %s/%s\n", SwitchImpl_AppDir(), out);
      return 1;
    }
  }

  /* 3. RomFS fallback for dev/CI builds that bake the ROM in. */
  for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
    snprintf(cand, sizeof(cand), "romfs:/%s", kNames[i]);
    if (probe_readable(cand, out, cap)) {
      fprintf(stderr, "[Switch] ROM (romfs): %s\n", cand);
      return 1;
    }
  }

  return 0;
}

void ThrowMissingROM(void) {
  FILE *f;

  /* Persist the fix next to the binary: hbmenu gives homebrew no
   * visible stderr, but sdmc files are always inspectable. */
  f = fopen("MISSING_ROM.txt", "w");
  if (f) {
    fprintf(f,
            "SNESRecomp (Switch): no ROM found.\n"
            "\n"
            "Copy your legally obtained ROM to:\n"
            "  sdmc:/switch/%s/rom.sfc\n"
            "(rom.smc with a copier header is also accepted)\n"
            "\n"
            "Searched: ./rom.sfc, ./rom.smc, ./game.sfc, ./game.smc,\n"
            "          romfs:/rom.sfc, romfs:/rom.smc.\n",
            SWITCH_APP_DIR);
    fclose(f);
  }

  fprintf(stderr,
          "[Switch] FATAL: no ROM found.\n"
          "[Switch] Place it at sdmc:/switch/%s/rom.sfc (see MISSING_ROM.txt)\n",
          SWITCH_APP_DIR);
  /* Give nxlink/USB log readers a beat to flush, then exit to hbmenu. */
  svcSleepThread(3 * 1000000000ULL);
  exit(1);
}

#endif /* __SWITCH__ */
