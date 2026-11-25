/* Synthetic regression for SNES OBJ range/fetch ordering.
 * No game ROM, generated data, or platform frontend is required. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "snes/ppu.h"
#include "snes/snes.h"

Snes *g_snes;

/* ppu.c references these runtime globals, but this harness links ppu.c alone. */
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;

static bool g_shadow_active;
static unsigned g_shadow_tile_calls;
static uint16_t g_shadow_tile;

uint16_t WsShadowTile(int layer, int screen_x, uint32_t wrapped_y,
                      uint16_t h_scroll, uint16_t tilemap_adr,
                      uint16_t real_tile) {
    (void)layer;
    (void)screen_x;
    (void)wrapped_y;
    (void)h_scroll;
    (void)tilemap_adr;
    g_shadow_tile_calls++;
    return g_shadow_active && (screen_x < 0 || screen_x + 8 > kPpuXPixels)
               ? g_shadow_tile
               : real_tile;
}

int WsShadowNativeLeft(int layer) {
    (void)layer;
    return 0;
}

int WsShadowNativeRight(int layer) {
    (void)layer;
    return 256;
}

bool WsShadowLayerActive(int layer) {
    (void)layer;
    return g_shadow_active;
}

uint32_t WsShadowWorldX(int layer) {
    (void)layer;
    return 0;
}

int32_t WsShadowPresentWorldX(int layer, int screen_x, uint16_t h_scroll) {
    (void)layer;
    return screen_x + h_scroll;
}

uint32_t WsShadowPresentWorldY(int layer, int screen_x) {
    (void)layer;
    (void)screen_x;
    return 0;
}

uint32_t WsShadowScrollY(int layer) {
    (void)layer;
    return 0;
}

void WsShadowOnVramWrite(uint16_t word_adr, uint16_t value) {
    (void)word_adr;
    (void)value;
}

static int check(bool condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

static void no_op_line_enhancer(Ppu *ppu, uint y, bool sub, void *context) {
    (void)ppu;
    (void)y;
    (void)sub;
    (void)context;
}

static void setup_35_sliver_case(Ppu *ppu) {
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    memset(ppu->highOam, 0, sizeof ppu->highOam);
    ppu->obsel = 2 << 5;  /* size pair 8x8 / 64x64 */
    /* Fetch order is reversed. Slots 1..4 contribute 32 slivers and slots
     * 5..6 contribute two more, so native fetch drops slot 0 as sliver 35. */
    ppu->oam[0] = 0x0000;
    for (int slot = 1; slot <= 6; slot++) {
        ppu->oam[slot * 2] = 0x0040;
        if (slot <= 4) {
            int high_byte = slot >> 2;
            int size_bit = ((slot & 3) * 2) + 1;
            ppu->highOam[high_byte] |= (uint8_t)(1u << size_bit);
        }
    }
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
        ppu->vram[i] = 0xffff;
}

static void setup_33_sprite_case(Ppu *ppu) {
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    memset(ppu->highOam, 0, sizeof ppu->highOam);
    ppu->obsel = 0;  /* 8x8 / 16x16; all entries use the small size */
    for (int slot = 0; slot < 33; slot++)
        ppu->oam[slot * 2] = 0x0040;
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
        ppu->vram[i] = 0xffff;
}

int main(void) {
    enum { kPitch = kPpuXPixels * 4 };
    uint8_t pixels[kPitch];
    Ppu *ppu = ppu_init();
    int failures = 0;
    if (!ppu) return 2;
    memset(pixels, 0, sizeof pixels);
    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch, kPpuRenderFlags_NewRenderer);
    ppu->inidisp = 0x0f;

    /* Keep unused OAM entries off this line. Slot 0 is one 8x8 sprite at x=0;
     * slots 1..5 are 64x64 sprites at x=64. Reverse tile fetch reaches the
     * 34-sliver limit before slot 0, while a forward one-pass implementation
     * incorrectly renders it. */
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    ppu->obsel = 2 << 5;  /* size pair 8x8 / 64x64 */
    ppu->oam[0] = 0x0000;
    for (int slot = 1; slot <= 5; slot++) {
        ppu->oam[slot * 2] = 0x0040;
        int high_byte = slot >> 2;
        int size_bit = ((slot & 3) * 2) + 1;
        ppu->highOam[high_byte] |= (uint8_t)(1u << size_bit);
    }
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
        ppu->vram[i] = 0xffff;

    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check(ppu->timeOver, "34-sliver overflow is reported");
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) == 0,
                      "reverse fetch drops low slot after sliver overflow");

    PpuBeginDrawing(ppu, pixels, kPitch,
                    kPpuRenderFlags_NewRenderer |
                    kPpuRenderFlags_NoSpriteLimits);
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) != 0,
                      "disabling sprite limits renders the low slot");

    /* A live widened world gets only the capacity represented by added
     * columns. The same flag with zero live columns (including a centered
     * fixed screen) must retain the exact native limits. Uses its own
     * scratch so the shared native scanline above stays untouched. */
    {
        enum { kWideExtra = 8, kWidePitch = (kPpuXPixels + kWideExtra * 2) * 4 };
        uint8_t wide_scratch[kWidePitch];
        memset(wide_scratch, 0, sizeof wide_scratch);

        ppu_reset(ppu);
        PpuBeginDrawing(ppu, wide_scratch, kWidePitch,
                        kPpuRenderFlags_NewRenderer |
                        kPpuRenderFlags_WidescreenSpriteBudget);
        PpuSetExtraSpaceCentered(ppu, 8);
        ppu->inidisp = 0x0f;
        setup_35_sliver_case(ppu);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(ppu->timeOver,
                          "centered widescreen keeps native 34-sliver limit");
        failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) == 0,
                          "centered widescreen still drops sliver 35");

        ppu_reset(ppu);
        PpuBeginDrawing(ppu, wide_scratch, kWidePitch,
                        kPpuRenderFlags_NewRenderer |
                        kPpuRenderFlags_WidescreenSpriteBudget);
        PpuSetExtraSpace(ppu, 8);  /* 16 added pixels -> two added slivers */
        ppu->inidisp = 0x0f;
        setup_35_sliver_case(ppu);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(!ppu->timeOver,
                          "live 16-pixel margins admit sliver 35");
        failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) != 0,
                          "live widened world renders the formerly dropped slot");

        ppu_reset(ppu);
        PpuBeginDrawing(ppu, wide_scratch, kWidePitch,
                        kPpuRenderFlags_NewRenderer |
                        kPpuRenderFlags_WidescreenSpriteBudget);
        PpuSetExtraSpaceCentered(ppu, 8);
        ppu->inidisp = 0x0f;
        setup_33_sprite_case(ppu);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(ppu->rangeOver,
                          "centered widescreen keeps native 32-sprite limit");

        ppu_reset(ppu);
        PpuBeginDrawing(ppu, wide_scratch, kWidePitch,
                        kPpuRenderFlags_NewRenderer |
                        kPpuRenderFlags_WidescreenSpriteBudget);
        PpuSetExtraSpace(ppu, 8);
        ppu->inidisp = 0x0f;
        setup_33_sprite_case(ppu);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(!ppu->rangeOver,
                          "live 16-pixel margins admit sprite 33");
    }

    /* Existing line-enhancer users rely on BG1 staying inside its authentic
     * destination viewport. New title-specific source insets must be opt-in
     * and must not relax that legacy fallback. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLineEnhancer(ppu, no_op_line_enhancer, NULL);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] == 0 &&
                              wide_pixels[kExtra - 1] == 0 &&
                              wide_pixels[kExtra + kPpuXPixels] == 0,
                          "legacy line enhancer keeps BG1 out of margins");
        failures += check(wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels - 1] != 0,
                          "legacy line enhancer retains native BG1");

        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerViewportInset(ppu, 0, 16, 16);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[kExtra] == 0 &&
                              wide_pixels[kExtra + 15] == 0 &&
                              wide_pixels[kExtra + 240] == 0,
                          "explicit BG1 viewport inset hides native padding");
        failures += check(wide_pixels[kExtra + 16] != 0 &&
                              wide_pixels[kExtra + 239] != 0,
                          "explicit BG1 viewport inset retains visible span");

        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLineEnhancerWideLayers(ppu, 1u);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kExtra - 1] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels] != 0 &&
                              wide_pixels[kWidePixels - 1] != 0,
                          "line enhancer preserves opted-in wide BG1 margins");
    }

    /* Fine scroll can make one 8-pixel renderer chunk straddle either native
     * edge. World-shadow ownership begins in the host margin, not at the
     * chunk boundary: center pixels must keep the cartridge tile even when
     * the margin pixels beside them use a different shadow tile. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerMask(ppu, 1);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->bgXsc[0] = 0x08;  /* tilemap at VRAM word $0800 */
        ppu->hScroll[0] = 1;
        ppu->screenEnabled[0] = 1;
        for (int i = 0; i < 32 * 32; i++)
            ppu->vram[0x0800 + i] = 1;
        for (int row = 0; row < 8; row++) {
            ppu->vram[1 * 16 + row] = 0x00ff;  /* color 1 */
            ppu->vram[2 * 16 + row] = 0xff00;  /* color 2 */
        }
        ppu->cgram[0] = 0;
        ppu->cgram[1] = 0x001f;
        ppu->cgram[2] = 0x03e0;
        g_shadow_tile = 2;
        g_shadow_active = true;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[kExtra - 1] != 0 &&
                              wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra - 1] != wide_pixels[kExtra],
                          "left straddle changes only in the margin");
        failures += check(
            wide_pixels[kExtra + kPpuXPixels - 1] != 0 &&
                wide_pixels[kExtra + kPpuXPixels] != 0 &&
                wide_pixels[kExtra + kPpuXPixels - 1] !=
                    wide_pixels[kExtra + kPpuXPixels],
            "right straddle changes only in the margin");
        failures += check(wide_pixels[kExtra] ==
                              wide_pixels[kExtra + kPpuXPixels - 1],
                          "native center keeps the cartridge tile");
        failures += check(wide_pixels[0] == wide_pixels[kExtra - 1] &&
                              wide_pixels[kExtra + kPpuXPixels] ==
                                  wide_pixels[kWidePixels - 1],
                          "both margins keep the world-shadow tile");
        g_shadow_active = false;
    }

    /* A rendered-scanline repeat band must use authentic VRAM as its source,
     * even when the world shadow is active for other lines. An HDMA split can
     * give a layer a different role from the frame-keyed shadow; consulting
     * that shadow here both corrupts the native center and repeats the wrong
     * pixels into the margins. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerRepeatBand(ppu, 0, 1, 2);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        g_shadow_active = true;
        g_shadow_tile_calls = 0;
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(g_shadow_tile_calls == 0,
                          "repeat band bypasses world shadow");
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kExtra - 1] != 0 &&
                              wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels - 1] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels] != 0 &&
                              wide_pixels[kWidePixels - 1] != 0,
                          "repeat band preserves center and fills margins");
        g_shadow_active = false;
    }

    /* A raw band presents a 64-column map's own hardware wrap in the
     * margins: the world shadow is bypassed like a repeat band, and no
     * repeat of the native line replaces the rendered margin columns. */
    {
        enum { kExtra = 16, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerMask(ppu, 1);
        PpuSetWidescreenLayerRawBand(ppu, 0, 1, 2);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->bgXsc[0] = 0x09;  /* 64x32 tilemap at VRAM word $0800 */
        ppu->hScroll[0] = 0;
        ppu->screenEnabled[0] = 1;
        for (int i = 0; i < 32 * 32; i++) {
            ppu->vram[0x0800 + i] = 1;  /* left page: color 1 */
            ppu->vram[0x0c00 + i] = 2;  /* right page: color 2 */
        }
        for (int row = 0; row < 8; row++) {
            ppu->vram[1 * 16 + row] = 0x00ff;
            ppu->vram[2 * 16 + row] = 0xff00;
        }
        ppu->cgram[0] = 0;
        ppu->cgram[1] = 0x001f;
        ppu->cgram[2] = 0x03e0;
        g_shadow_tile = 3;
        g_shadow_active = true;
        g_shadow_tile_calls = 0;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(g_shadow_tile_calls == 0,
                          "raw band bypasses world shadow");
        failures += check(wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra] ==
                                  wide_pixels[kExtra + kPpuXPixels - 1],
                          "raw band keeps the native center");
        failures += check(wide_pixels[kExtra + kPpuXPixels] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels] !=
                                  wide_pixels[kExtra] &&
                              wide_pixels[kWidePixels - 1] ==
                                  wide_pixels[kExtra + kPpuXPixels],
                          "raw band right margin reads the map's second page");
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[0] != wide_pixels[kExtra] &&
                              wide_pixels[kExtra - 1] == wide_pixels[0],
                          "raw band left margin reads the map's hardware wrap");
        g_shadow_active = false;
    }

    /* A repeated layer whose rendered line proves a period continues that
     * period into the margins and, when asked, rebuilds its stale endpoint
     * pixels from the same period. A 12-tile (96-pixel) map on a 32-column
     * ring wraps at 256 on hardware; the auto period must not restart it. */
    {
        enum { kExtra = 16, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerRepeat(ppu, 1);
        PpuSetWidescreenLayerRepeatAutoPeriod(ppu, 1, 1);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->bgXsc[0] = 0x08;  /* 32x32 tilemap at VRAM word $0800 */
        ppu->hScroll[0] = 3;   /* fine phase: the stale tile shows 5 px */
        ppu->screenEnabled[0] = 1;
        /* Solid 4bpp characters: character n is filled with color n, so
         * column c (character (c % 12) + 1) shows twelve distinct colors and
         * the map period is 96 pixels. Column 0 holds a stale endpoint tile
         * whose color breaks that period. */
        for (int character = 1; character <= 15; character++) {
            for (int row = 0; row < 8; row++) {
                ppu->vram[character * 16 + row] = (uint16_t)(
                    ((character & 1) ? 0x00ff : 0) |
                    ((character & 2) ? 0xff00 : 0));
                ppu->vram[character * 16 + 8 + row] = (uint16_t)(
                    ((character & 4) ? 0x00ff : 0) |
                    ((character & 8) ? 0xff00 : 0));
            }
            ppu->cgram[character] = (uint16_t)(0x0421 * character);
        }
        for (int column = 0; column < 32; column++)
            ppu->vram[0x0800 + column] = (uint16_t)((column % 12) + 1);
        ppu->vram[0x0800] = 15;  /* stale endpoint tile, color 15 */
        ppu->cgram[0] = 0;
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        int periodic = 1;
        for (int x = 0; x + 96 < kWidePixels; x++) {
            if (wide_pixels[x] != wide_pixels[x + 96])
                periodic = 0;
        }
        failures += check(periodic &&
                              wide_pixels[kExtra + 8] != wide_pixels[kExtra + 16],
                          "auto period continues a 96-pixel line and "
                          "repairs its stale endpoint");
        PpuSetWidescreenLayerRepeatAutoPeriod(ppu, 1, 0);
        memset(wide_pixels, 0, sizeof wide_pixels);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[kExtra] != wide_pixels[kExtra + 96] &&
                              wide_pixels[kExtra - 1] ==
                                  wide_pixels[kExtra - 1 + 96] &&
                              wide_pixels[kExtra - 1] != 0,
                          "auto period without edge repair keeps the "
                          "native endpoint and still continues the margin");
        /* A presentation bias of +8 places the first eight columns of the
         * authentic 4:3 viewport in the left margin. They must render from
         * real VRAM (map columns 31 and the stale column 0 at fine phase 3),
         * not from the period continuation; columns beyond them continue. */
        PpuSetWidescreenPresentationXBias(ppu, 8);
        /* The repeated layer is bounded, so a game keeps it outside the
         * widen mask; the authentic columns must render regardless. */
        PpuSetWidescreenLayerMask(ppu, 2);
        memset(wide_pixels, 0, sizeof wide_pixels);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[kExtra - 8] == wide_pixels[kExtra + 248] &&
                              wide_pixels[kExtra - 4] ==
                                  wide_pixels[kExtra + 252] &&
                              wide_pixels[kExtra - 3] == wide_pixels[kExtra] &&
                              wide_pixels[kExtra - 3] !=
                                  wide_pixels[kExtra - 4] &&
                              wide_pixels[kExtra - 16] ==
                                  wide_pixels[kExtra - 16 + 96],
                          "biased 4:3 columns render authentically inside a "
                          "repeated layer");
        PpuSetWidescreenPresentationXBias(ppu, 0);
        PpuSetWidescreenLayerMask(ppu, 0);

        /* A 64-column allocation whose second page is a stale ring page of
         * one flat color from map column 33 on. The synthetic scroll is not
         * shifted by the bias here, so screen x shows map pixel x+3: the
         * authentic window [-8, 248) begins in the stale wrap of column 63
         * and the stale endpoint column 0 (screen -8..4), and the PPU's last
         * native columns from 253 fall on the stale page. Those must continue
         * the authentic window's 96-pixel period instead of copying the page,
         * while every authentic column renders as it is. */
        ppu->bgXsc[0] = 0x01 | (0x0800 >> 8);  /* 64 columns, same base */
        ppu->vram[0x0c00] = (uint16_t)((32 % 12) + 1);
        for (int column = 1; column < 32; column++)
            ppu->vram[0x0c00 + column] = 15;   /* stale second page */
        ppu->hScroll[0] = 3;
        PpuSetWidescreenPresentationXBias(ppu, 8);
        PpuSetWidescreenLayerMask(ppu, 2);
        memset(wide_pixels, 0, sizeof wide_pixels);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        {
            const uint32_t stale = wide_pixels[kExtra - 6];  /* map col 0 */
            int continued = 1;
            for (int x = 253; x < kPpuXPixels + kExtra; x++)
                continued &= wide_pixels[kExtra + x] ==
                                 wide_pixels[kExtra + x - 96] &&
                             wide_pixels[kExtra + x] != stale;
            failures += check(continued &&
                                  wide_pixels[kExtra - 8] ==
                                      wide_pixels[kExtra + 4] &&
                                  wide_pixels[kExtra + 4] !=
                                      wide_pixels[kExtra + 5] &&
                                  wide_pixels[kExtra + 100] ==
                                      wide_pixels[kExtra + 196],
                              "a 64-column ring's stale tail past the "
                              "biased authentic window is continued");
        }
        PpuSetWidescreenPresentationXBias(ppu, 0);
        PpuSetWidescreenLayerMask(ppu, 0);
        ppu->bgXsc[0] = (0x0800 >> 8);
    }

    /* Presentation bias is host-only and bounded by the renderer's fixed
     * margin capacity. Background scroll is shifted by the game frontend;
     * the shared PPU stores the matching OBJ correction. */
    ppu_reset(ppu);
    PpuSetWidescreenPresentationXBias(ppu, 26);
    failures += check(ppu->wsPresentationXBias == 26,
                      "presentation bias accepts in-range correction");
    PpuSetWidescreenPresentationXBias(ppu, kPpuExtraLeftRight + 10);
    failures += check(ppu->wsPresentationXBias == kPpuExtraLeftRight,
                      "presentation bias clamps positive correction");
    PpuSetWidescreenPresentationXBias(ppu, -kPpuExtraLeftRight - 10);
    failures += check(ppu->wsPresentationXBias == -kPpuExtraLeftRight,
                      "presentation bias clamps negative correction");

    /* The parity renderer is also SMK's widescreen Mode 7 path. Live margins
     * must sample real map coordinates, while centered extra space remains
     * an exact native-width presentation for menus. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels, 0);
        PpuSetExtraSpace(ppu, kExtra);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 7;
        ppu->screenEnabled[0] = 1;
        ppu->m7matrix[0] = 0x0100;
        ppu->m7matrix[3] = 0x0100;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0x0101;
        ppu->cgram[1] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kExtra - 1] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels] != 0 &&
                              wide_pixels[kWidePixels - 1] != 0,
                          "legacy Mode 7 renders live side margins");

        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] == 0 &&
                              wide_pixels[kExtra - 1] == 0 &&
                              wide_pixels[kExtra + kPpuXPixels] == 0 &&
                              wide_pixels[kWidePixels - 1] == 0,
                          "legacy Mode 7 preserves centered margins");
        failures += check(wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels - 1] != 0,
                          "legacy Mode 7 retains centered native columns");
    }

    /* Legacy-renderer titles use the same opt-in HUD anchor bands as the
     * current renderer. The inserted spans are transparent, while the source
     * pixels on either side retain their distance from the window edges. */
    {
        enum {
            kExtra = 8,
            kLeftEnd = 32,
            kRightStart = 224,
            kWidePixels = kPpuXPixels + kExtra * 2
        };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels, 0);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerAnchorBandSlot(
            ppu, 1, 1, 0, 2, kLeftEnd, kRightStart);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1 << 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(
            wide_pixels[0] != 0 &&
                wide_pixels[kLeftEnd - 1] != 0 &&
                wide_pixels[kExtra + kLeftEnd] != 0 &&
                wide_pixels[kExtra + kRightStart - 1] != 0 &&
                wide_pixels[kExtra * 2 + kRightStart] != 0 &&
                wide_pixels[kWidePixels - 1] != 0,
            "legacy HUD anchors preserve left, center, and right spans");
        failures += check(
            wide_pixels[kLeftEnd] == 0 &&
                wide_pixels[kExtra + kLeftEnd - 1] == 0 &&
                wide_pixels[kExtra + kRightStart] == 0 &&
                wide_pixels[kExtra * 2 + kRightStart - 1] == 0,
            "legacy HUD anchors leave inserted margin spans transparent");
    }

    /* A room boundary may deliberately leave the live world margins empty,
     * while a title-specific HUD still uses the full centering budget. Verify
     * both supported HUD sources: an anchored 4bpp background and explicitly
     * selected OAM slots. Unselected world sprites must remain centered. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        PpuSetWidescreenLayerAnchorBand(ppu, 1, 0, 2, 32, 224);
        PpuSetWidescreenHudAlwaysVisible(ppu, true);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1 << 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kWidePixels - 1] != 0,
                          "always-visible BG HUD uses full centered margins");

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        PpuSetWsHudOamBand(ppu, 2, 112, 160);
        PpuSetWsHudOamShiftRange(ppu, 0, 1);
        PpuSetWidescreenHudAlwaysVisible(ppu, true);
        ppu->inidisp = 0x0f;
        ppu->screenEnabled[0] = 1 << 4;
        for (int slot = 0; slot < 128; slot++)
            ppu->oam[slot * 2] = 0xf000;
        ppu->oam[0] = 0x0000;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kExtra] == 0,
                          "selected HUD OAM shifts into centered margin");

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        PpuSetWsHudOamBand(ppu, 2, 112, 160);
        PpuSetWsHudOamShiftRange(ppu, 1, 1);
        PpuSetWidescreenHudAlwaysVisible(ppu, true);
        ppu->inidisp = 0x0f;
        ppu->screenEnabled[0] = 1 << 4;
        for (int slot = 0; slot < 128; slot++)
            ppu->oam[slot * 2] = 0xf000;
        ppu->oam[0] = 0x0000;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] == 0 &&
                              wide_pixels[kExtra] != 0,
                          "unselected world OAM remains centered");
    }

    /* A 2bpp BG3 (mode 1) under the world-keyed shadow renders its margins
     * from the shadow tile and splits the chunk straddling the authentic
     * boundary per pixel, exactly like the 4bpp layers (DKC2's ship-deck
     * rigging is such a layer). */
    {
        enum { kExtra = 16, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerMask(ppu, 0x04);
        PpuSetWidescreenBg3Widen(ppu, 1);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->bgXsc[2] = 0x09;  /* 64x32 tilemap at VRAM word $0800 */
        ppu->hScroll[2] = 3;   /* the last native chunk straddles x=256 */
        ppu->screenEnabled[0] = 1 << 2;
        /* 2bpp characters: character 1 is solid color 1, character 2 is
         * solid color 2. Every ring column holds character 1. */
        for (int row = 0; row < 8; row++) {
            ppu->vram[1 * 8 + row] = 0x00ff;
            ppu->vram[2 * 8 + row] = 0xff00;
        }
        ppu->cgram[0] = 0;
        ppu->cgram[1] = 0x001f;
        ppu->cgram[2] = 0x03e0;
        for (int column = 0; column < 64; column++)
            ppu->vram[0x0800 + (column & 31) + (column >= 32 ? 0x400 : 0)] = 1;
        g_shadow_active = true;
        g_shadow_tile = 2;
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        const uint32_t center = wide_pixels[kExtra + 8];
        const uint32_t margin = wide_pixels[0];
        failures += check(center != 0 && margin != 0 && center != margin &&
                              wide_pixels[kExtra] == center &&
                              wide_pixels[kExtra + kPpuXPixels - 1] == center &&
                              wide_pixels[kExtra - 1] == margin &&
                              wide_pixels[kExtra + kPpuXPixels] == margin &&
                              wide_pixels[kWidePixels - 1] == margin,
                          "2bpp layer renders shadow margins and splits the "
                          "boundary chunk per pixel");
        g_shadow_active = false;
    }

    /* A positive presentation bias shifts objects left by the bias, so a
     * game places objects for the presented right margin up to the bias
     * beyond the authentic margin. Such a nine-bit X is still positive:
     * with a 16-pixel margin and a bias of 8, X = 278 presents at 270. */
    {
        enum { kExtra = 16, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenPresentationXBias(ppu, 8);
        ppu->inidisp = 0x0f;
        ppu->screenEnabled[0] = 1 << 4;
        for (int slot = 0; slot < 128; slot++)
            ppu->oam[slot * 2] = 0xf000;
        ppu->oam[0] = (uint16_t)(278 & 0xff);  /* y 0, x low byte */
        ppu->highOam[0] |= 1;                    /* x bit 8 */
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[kExtra + 270] != 0 &&
                              wide_pixels[kExtra + 269] == 0,
                          "positive bias keeps an object placed for the "
                          "presented right margin on the right");
        PpuSetWidescreenPresentationXBias(ppu, 0);
    }

    ppu_free(ppu);
    if (failures) return 1;
    puts("ppu_sprite_limit_test: PASS");
    return 0;
}
