#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "gc9a01.h"

#define GFX_RGB(r, g, b) ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))

#define GFX_BLACK   GFX_RGB(0, 0, 0)
#define GFX_WHITE   GFX_RGB(255, 255, 255)
#define GFX_GREY    GFX_RGB(120, 120, 130)
#define GFX_RED     GFX_RGB(230, 40, 40)
#define GFX_GREEN   GFX_RGB(40, 200, 90)
#define GFX_BLUE    GFX_RGB(40, 110, 230)
#define GFX_AMBER   GFX_RGB(250, 170, 20)

/* Glyph cell: 5x7 font + 1 px spacing, multiplied by the scale factor. */
#define GFX_CHAR_W(scale) (6U * (scale))
#define GFX_CHAR_H(scale) (8U * (scale))

void       GFX_Init(LCD_Handle *lcd);
LCD_Status GFX_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
LCD_Status GFX_FillScreen(uint16_t color);
LCD_Status GFX_FillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
LCD_Status GFX_FillRing(int16_t cx, int16_t cy, int16_t r_out, int16_t r_in, uint16_t color);
LCD_Status GFX_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, uint16_t color);

/* UTF-8 text, opaque (bg is painted too) so redrawing over old text does not flicker. */
uint16_t   GFX_TextWidth(const char *utf8, uint8_t scale);
LCD_Status GFX_DrawText(int16_t x, int16_t y, const char *utf8, uint8_t scale, uint16_t fg, uint16_t bg);
/* Centered horizontally; if pad_w > 0 the text is padded with bg to pad_w px to wipe longer old text. */
LCD_Status GFX_DrawTextCentered(int16_t y, const char *utf8, uint8_t scale, uint16_t fg, uint16_t bg, uint16_t pad_w);

#endif
