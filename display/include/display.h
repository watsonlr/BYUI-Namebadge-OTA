/**
 * @file display.h
 * @brief ILI9341 TFT display driver — SPI2, landscape 320×240.
 *
 * Provides display initialisation, fill, and text rendering using
 * a built-in 8×8 bitmap font with configurable integer scaling.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Display dimensions (landscape) ─────────────────────────────── */
#define DISPLAY_W  320
#define DISPLAY_H  240

/* ── Font cell size ──────────────────────────────────────────────── */
#define DISPLAY_FONT_W  8
#define DISPLAY_FONT_H  8

/* ── Colour helpers ──────────────────────────────────────────────── */
/** Pack r,g,b (0-255) into RGB565. */
#define DISPLAY_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

#define DISPLAY_COLOR_BLACK   ((uint16_t)0x0000)
#define DISPLAY_COLOR_WHITE   ((uint16_t)0xFFFF)
#define DISPLAY_COLOR_RED     DISPLAY_RGB565(255,   0,   0)
#define DISPLAY_COLOR_GREEN   DISPLAY_RGB565(  0, 255,   0)
#define DISPLAY_COLOR_BLUE    DISPLAY_RGB565(  0,   0, 255)
#define DISPLAY_COLOR_YELLOW  DISPLAY_RGB565(255, 255,   0)
#define DISPLAY_COLOR_CYAN    DISPLAY_RGB565(  0, 255, 255)

/**
 * @brief Initialise SPI2 bus and ILI9341 display.
 * Must be called once before any other display function.
 */
void display_init(void);

/**
 * @brief Fill the entire screen with a solid colour.
 * @param color  RGB565 colour value (host byte-order).
 */
void display_fill(uint16_t color);

/**
 * @brief Draw a single ASCII character at pixel position (x, y).
 *
 * @param x      Left edge of the character cell.
 * @param y      Top edge of the character cell.
 * @param c      ASCII character (0x20–0x7F).
 * @param fg     Foreground RGB565 colour.
 * @param bg     Background RGB565 colour.
 * @param scale  Integer pixel scale (1 = 8×8, 2 = 16×16, 3 = 24×24 …).
 */
void display_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);

/**
 * @brief Draw a NUL-terminated ASCII string starting at (x, y).
 * Characters are placed left-to-right with no word-wrap.
 *
 * @param x      Left edge of the first character cell.
 * @param y      Top edge of the text row.
 * @param str    NUL-terminated ASCII string.
 * @param fg     Foreground RGB565 colour.
 * @param bg     Background RGB565 colour.
 * @param scale  Integer pixel scale.
 */
void display_draw_string(int x, int y, const char *str,
                         uint16_t fg, uint16_t bg, int scale);

#ifdef __cplusplus
}
#endif
