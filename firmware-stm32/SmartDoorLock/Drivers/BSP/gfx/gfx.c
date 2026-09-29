#include "gfx.h"
#include "font5x7.h"

/* 10 display lines; also large enough for one glyph cell at scale 4 (24 x 32 px).
 * Static: DMA reads it after the call that filled it returns control to the driver. */
#define GFX_BUF_PX  (LCD_WIDTH * 10U)
#define GFX_MAX_TEXT_SCALE 4U

static LCD_Handle *s_lcd;
static uint16_t s_buf[GFX_BUF_PX];

void GFX_Init(LCD_Handle *lcd)
{
    s_lcd = lcd;
}

static void fill_buf(uint16_t color, uint32_t n)
{
    uint16_t be = LCD_Swap16(color);
    for (uint32_t i = 0; i < n; i++) {
        s_buf[i] = be;
    }
}

LCD_Status GFX_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if (x + w > (int16_t)LCD_WIDTH)  { w = (int16_t)(LCD_WIDTH - (uint16_t)x); }
    if (y + h > (int16_t)LCD_HEIGHT) { h = (int16_t)(LCD_HEIGHT - (uint16_t)y); }
    if (w <= 0 || h <= 0) {
        return LCD_OK;
    }

    uint32_t total = (uint32_t)w * (uint32_t)h;
    uint32_t chunk = (total < GFX_BUF_PX) ? total : GFX_BUF_PX;
    fill_buf(color, chunk);

    LCD_Status st = LCD_SetWindow(s_lcd, (uint16_t)x, (uint16_t)y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    while (st == LCD_OK && total > 0U) {
        uint32_t n = (total < chunk) ? total : chunk;
        st = LCD_WritePixels(s_lcd, s_buf, n);
        total -= n;
    }
    return st;
}

LCD_Status GFX_FillScreen(uint16_t color)
{
    return GFX_FillRect(0, 0, (int16_t)LCD_WIDTH, (int16_t)LCD_HEIGHT, color);
}

static int16_t isqrt(int32_t v)
{
    int32_t r = 0;
    while ((r + 1) * (r + 1) <= v) {
        r++;
    }
    return (int16_t)r;
}

LCD_Status GFX_FillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color)
{
    return GFX_FillRing(cx, cy, r, 0, color);
}

LCD_Status GFX_FillRing(int16_t cx, int16_t cy, int16_t r_out, int16_t r_in, uint16_t color)
{
    LCD_Status st = LCD_OK;
    int32_t ro2 = (int32_t)r_out * r_out;
    int32_t ri2 = (int32_t)r_in * r_in;
    for (int16_t dy = (int16_t)-r_out; dy <= r_out && st == LCD_OK; dy++) {
        int32_t dy2 = (int32_t)dy * dy;
        int16_t dx_out = isqrt(ro2 - dy2);
        int16_t y = (int16_t)(cy + dy);
        if (r_in > 0 && dy2 < ri2) {
            int16_t dx_in = isqrt(ri2 - dy2);
            st = GFX_FillRect((int16_t)(cx - dx_out), y, (int16_t)(dx_out - dx_in), 1, color);
            if (st == LCD_OK) {
                st = GFX_FillRect((int16_t)(cx + dx_in + 1), y, (int16_t)(dx_out - dx_in), 1, color);
            }
        } else {
            st = GFX_FillRect((int16_t)(cx - dx_out), y, (int16_t)(2 * dx_out + 1), 1, color);
        }
    }
    return st;
}

LCD_Status GFX_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, uint16_t color)
{
    int16_t dx = (int16_t)((x1 > x0) ? (x1 - x0) : (x0 - x1));
    int16_t dy = (int16_t)-((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);
    int16_t half = (int16_t)(thickness / 2U);
    LCD_Status st = LCD_OK;

    for (;;) {
        st = GFX_FillRect((int16_t)(x0 - half), (int16_t)(y0 - half), thickness, thickness, color);
        if (st != LCD_OK || (x0 == x1 && y0 == y1)) {
            break;
        }
        int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
        if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
    }
    return st;
}

static uint32_t utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp;
    uint8_t extra;
    if (p[0] < 0x80U)                { cp = p[0];         extra = 0; }
    else if ((p[0] & 0xE0U) == 0xC0U) { cp = p[0] & 0x1FU; extra = 1; }
    else if ((p[0] & 0xF0U) == 0xE0U) { cp = p[0] & 0x0FU; extra = 2; }
    else if ((p[0] & 0xF8U) == 0xF0U) { cp = p[0] & 0x07U; extra = 3; }
    else { *s += 1; return '?'; }
    for (uint8_t i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0U) != 0x80U) {
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3FU);
    }
    *s += 1U + extra;
    return cp;
}

uint16_t GFX_TextWidth(const char *utf8, uint8_t scale)
{
    uint16_t n = 0;
    while (*utf8 != '\0') {
        (void)utf8_next(&utf8);
        n++;
    }
    return (n == 0U) ? 0U : (uint16_t)(n * GFX_CHAR_W(scale) - scale);
}

static LCD_Status draw_glyph(int16_t x, int16_t y, uint32_t cp, uint8_t scale, uint16_t fg_be, uint16_t bg_be)
{
    const uint8_t *g = FONT5X7_Glyph(cp);
    uint16_t cw = (uint16_t)GFX_CHAR_W(scale);
    uint16_t ch = (uint16_t)GFX_CHAR_H(scale);
    if (x < 0 || y < 0 || x + cw > (int16_t)LCD_WIDTH || y + ch > (int16_t)LCD_HEIGHT) {
        return LCD_OK;
    }
    uint16_t *p = s_buf;
    for (uint16_t py = 0; py < ch; py++) {
        uint8_t row = (uint8_t)(py / scale);
        for (uint16_t px = 0; px < cw; px++) {
            uint8_t col = (uint8_t)(px / scale);
            bool on = (col < FONT5X7_W) && (row < FONT5X7_H) && ((g[col] >> row) & 1U);
            *p++ = on ? fg_be : bg_be;
        }
    }
    LCD_Status st = LCD_SetWindow(s_lcd, (uint16_t)x, (uint16_t)y, (uint16_t)(x + cw - 1), (uint16_t)(y + ch - 1));
    if (st == LCD_OK) {
        st = LCD_WritePixels(s_lcd, s_buf, (uint32_t)cw * ch);
    }
    return st;
}

LCD_Status GFX_DrawText(int16_t x, int16_t y, const char *utf8, uint8_t scale, uint16_t fg, uint16_t bg)
{
    if (scale == 0U) {
        scale = 1U;
    }
    if (scale > GFX_MAX_TEXT_SCALE) {
        scale = GFX_MAX_TEXT_SCALE;
    }
    uint16_t fg_be = LCD_Swap16(fg);
    uint16_t bg_be = LCD_Swap16(bg);
    LCD_Status st = LCD_OK;
    while (*utf8 != '\0' && st == LCD_OK) {
        uint32_t cp = utf8_next(&utf8);
        st = draw_glyph(x, y, cp, scale, fg_be, bg_be);
        x = (int16_t)(x + GFX_CHAR_W(scale));
    }
    return st;
}

LCD_Status GFX_DrawTextCentered(int16_t y, const char *utf8, uint8_t scale, uint16_t fg, uint16_t bg, uint16_t pad_w)
{
    uint16_t w = GFX_TextWidth(utf8, scale);
    int16_t x = (int16_t)((int16_t)LCD_WIDTH - (int16_t)w) / 2;
    LCD_Status st = LCD_OK;
    if (pad_w > w) {
        int16_t px = (int16_t)((int16_t)LCD_WIDTH - (int16_t)pad_w) / 2;
        uint16_t h = (uint16_t)GFX_CHAR_H(scale);
        st = GFX_FillRect(px, y, (int16_t)(x - px), (int16_t)h, bg);
        if (st == LCD_OK) {
            st = GFX_FillRect((int16_t)(x + (int16_t)w), y, (int16_t)(px + (int16_t)pad_w - x - (int16_t)w), (int16_t)h, bg);
        }
    }
    if (st == LCD_OK) {
        st = GFX_DrawText(x, y, utf8, scale, fg, bg);
    }
    return st;
}
