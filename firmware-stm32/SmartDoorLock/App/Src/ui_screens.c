#include <stdio.h>
#include "gfx.h"
#include "app_config.h"
#include "event_log.h"
#include "menu.h"
#include "ui.h"

#define CX            120
#define ICON_Y        70
#define RING_OUT      119
#define RING_IN       111
/* 3 рядки меню: тоді навіть найдовший текст уміщається у внутрішнє коло (r = 111) */
#define MENU_ROWS     3
#define MENU_ROW_H    38
#define MENU_FIRST_Y  70
#define MENU_BAR_X    26
#define MENU_BAR_W    188

static void text(int16_t y, const char *s, uint8_t scale, uint16_t fg)
{
    (void)GFX_DrawTextCentered(y, s, scale, fg, GFX_BLACK, 0);
}

/* з доповненням фоном: коротше нове значення повністю закриває довше старе */
static void text_pad(int16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t pad_w)
{
    (void)GFX_DrawTextCentered(y, s, scale, fg, GFX_BLACK, pad_w);
}

static void frame(uint16_t ring)
{
    (void)GFX_FillScreen(GFX_BLACK);
    (void)GFX_FillRing(CX, CX, RING_OUT, RING_IN, ring);
}

static void icon_lock(uint16_t c, bool open)
{
    if (open) {
        (void)GFX_FillRing(CX, ICON_Y - 12, 20, 12, c);
        (void)GFX_FillRect(CX - 20, ICON_Y - 12, 9, 14, GFX_BLACK);
    } else {
        (void)GFX_FillRing(CX, ICON_Y, 20, 12, c);
    }
    (void)GFX_FillRect(CX - 26, ICON_Y, 52, 38, c);
    (void)GFX_FillCircle(CX, ICON_Y + 15, 5, GFX_BLACK);
    (void)GFX_FillRect(CX - 2, ICON_Y + 15, 5, 13, GFX_BLACK);
}

static void icon_cross(uint16_t c)
{
    (void)GFX_DrawLine(CX - 22, ICON_Y - 16, CX + 22, ICON_Y + 28, 9, c);
    (void)GFX_DrawLine(CX + 22, ICON_Y - 16, CX - 22, ICON_Y + 28, 9, c);
}

static void icon_check(uint16_t c)
{
    (void)GFX_DrawLine(CX - 24, ICON_Y + 8, CX - 8, ICON_Y + 26, 9, c);
    (void)GFX_DrawLine(CX - 8, ICON_Y + 26, CX + 26, ICON_Y - 14, 9, c);
}

static void icon_bang(uint16_t c)
{
    (void)GFX_FillRect(CX - 6, ICON_Y - 20, 12, 34, c);
    (void)GFX_FillRect(CX - 6, ICON_Y + 22, 12, 12, c);
}

static void icon_card(uint16_t c)
{
    (void)GFX_FillRect(CX - 32, ICON_Y - 18, 64, 42, c);
    (void)GFX_FillRect(CX - 28, ICON_Y - 14, 56, 34, GFX_BLACK);
    (void)GFX_FillRect(CX - 22, ICON_Y - 6, 14, 12, c);
}

static const char *result_text(uint8_t r)
{
    switch ((UiResult)r) {
    case RES_CARD_ADDED:     return "Картку додано";
    case RES_ADMIN_ADDED:    return "Адміна додано";
    case RES_CARD_REMOVED:   return "Картку видалено";
    case RES_SAVED:          return "Збережено";
    case RES_ALREADY_EXISTS: return "Картка вже є";
    case RES_LIST_FULL:      return "Список повний";
    case RES_LIST_EMPTY:     return "Список порожній";
    case RES_LAST_ADMIN:     return "Останній адмін";
    case RES_STORAGE_ERROR:  return "Помилка Flash";
    case RES_RANDOM_UID:     return "Змінний UID";
    default:                 return "";
    }
}

static const char *event_text(uint16_t type)
{
    static const char *const names[LOGE_COUNT] = {
        [LOGE_BOOT]             = "СТАРТ",
        [LOGE_ACCESS_GRANTED]   = "ДОЗВОЛЕНО",
        [LOGE_ACCESS_DENIED]    = "ВІДМОВА",
        [LOGE_LOCKOUT]          = "БЛОКУВАННЯ",
        [LOGE_EXIT_BUTTON]      = "КНОПКА ВИХОДУ",
        [LOGE_ADMIN_LOGIN]      = "ВХІД АДМІНА",
        [LOGE_CARD_ADDED]       = "КАРТКУ ДОДАНО",
        [LOGE_CARD_REMOVED]     = "КАРТКУ ВИДАЛЕНО",
        [LOGE_RFID_FAULT]       = "ЗБІЙ RFID",
        [LOGE_RFID_RECOVERED]   = "RFID ВІДНОВЛЕНО",
        [LOGE_NET_UNLOCK]       = "З ТЕЛЕФОНА",
        [LOGE_SETTINGS_CHANGED] = "НАЛАШТУВАННЯ",
        [LOGE_STORAGE_CORRUPT]  = "FLASH ПОШКОДЖЕНО",
        [LOGE_WATCHDOG_RESET]   = "WATCHDOG",
    };
    return (type < LOGE_COUNT) ? names[type] : "?";
}

static void draw_menu_rows(const UiCommand *c)
{
    for (int row = 0; row < MENU_ROWS; row++) {
        int item = (int)c->sel - MENU_ROWS / 2 + row;
        int16_t y = (int16_t)(MENU_FIRST_Y + row * MENU_ROW_H);
        bool selected = (item == (int)c->sel);
        uint16_t bg = selected ? GFX_BLUE : GFX_BLACK;
        (void)GFX_FillRect(MENU_BAR_X, (int16_t)(y - 6), MENU_BAR_W, 28, bg);
        if (item >= 0 && item < (int)c->count) {
            (void)GFX_DrawTextCentered(y, MENU_Label((MenuItem)item), 2,
                                       selected ? GFX_WHITE : GFX_GREY, bg, 0);
        }
    }
    char buf[12];
    (void)snprintf(buf, sizeof(buf), "%u/%u", (unsigned)c->sel + 1U, (unsigned)c->count);
    text_pad(184, buf, 1, GFX_GREY, 60);
}

static void draw_delete_dynamic(const UiCommand *c)
{
    char buf[16];
    (void)snprintf(buf, sizeof(buf), "%u / %u", (unsigned)c->sel + 1U, (unsigned)c->count);
    text_pad(64, buf, 2, GFX_WHITE, 160);
    text_pad(94, c->line1, 3, GFX_WHITE, 200);
    text_pad(134, c->line2, 2, GFX_GREY, 200);
}

static void draw_log_dynamic(const UiCommand *c)
{
    char buf[16];
    if (c->count == 0U) {
        text_pad(64, "", 2, GFX_WHITE, 160);
        text_pad(100, "Порожньо", 2, GFX_GREY, 200);
        return;
    }
    (void)snprintf(buf, sizeof(buf), "%u / %u", (unsigned)c->sel + 1U, (unsigned)c->count);
    text_pad(64, buf, 2, GFX_WHITE, 160);
    text_pad(92, c->line1, 2, GFX_GREY, 200);
    text_pad(118, event_text(c->value), 2, GFX_WHITE, 216);
    text_pad(144, c->line2, 2, GFX_GREY, 200);
}

static void draw_settings_dynamic(const UiCommand *c)
{
    char buf[24];
    (void)snprintf(buf, sizeof(buf), "< %u с >", (unsigned)c->value);
    text_pad(96, buf, 4, GFX_WHITE, 200);
}

static void draw_lockout_dynamic(const UiCommand *c)
{
    char buf[40];
    (void)snprintf(buf, sizeof(buf), "Зачекайте %u с", (unsigned)c->value);
    text_pad(172, buf, 2, GFX_WHITE, 176);
}

static bool same_screen(const UiCommand *c, const UiCommand *prev)
{
    if (prev == NULL || prev->screen != c->screen) {
        return false;
    }
    if (c->screen == SCR_RESULT_OK || c->screen == SCR_RESULT_FAIL) {
        return prev->result == c->result;
    }
    if (c->screen == SCR_SETUP || c->screen == SCR_ENROLL) {
        return prev->value == c->value;
    }
    return true;
}

void UI_Draw(const UiCommand *c, const UiCommand *prev)
{
    bool same = same_screen(c, prev);

    switch (c->screen) {
    case SCR_BOOT:
        if (!same) {
            frame(GFX_BLUE);
            text(100, "SmartDoorLock", 2, GFX_WHITE);
            text(128, "v" APP_VERSION, 1, GFX_GREY);
        }
        break;

    case SCR_LOCKED:
        if (!same) {
            frame(GFX_BLUE);
            icon_lock(GFX_WHITE, false);
            text(126, "ЗАЧИНЕНО", 3, GFX_WHITE);
            text(168, "Прикладіть", 2, GFX_GREY);
            text(188, "картку", 2, GFX_GREY);
        }
        break;

    case SCR_GRANTED:
        if (!same) {
            frame(GFX_GREEN);
            icon_lock(GFX_GREEN, true);
            text(126, "ВІДЧИНЕНО", 3, GFX_GREEN);
            text(164, "Ласкаво просимо", 2, GFX_WHITE);
        }
        break;

    case SCR_DENIED:
        if (!same) {
            frame(GFX_RED);
            icon_cross(GFX_RED);
            text(126, "ВІДМОВА", 3, GFX_RED);
            text(164, "Невідома картка", 2, GFX_WHITE);
        }
        break;

    case SCR_LOCKOUT:
        if (!same) {
            frame(GFX_RED);
            icon_bang(GFX_RED);
            text(126, "БЛОКУВАННЯ", 3, GFX_RED);
        }
        draw_lockout_dynamic(c);
        break;

    case SCR_FAULT:
        if (!same) {
            frame(GFX_AMBER);
            icon_bang(GFX_AMBER);
            text(126, "ЗБІЙ", 3, GFX_AMBER);
            text(168, "Зчитувач RFID", 2, GFX_WHITE);
            text(188, "недоступний", 2, GFX_GREY);
        }
        break;

    case SCR_SETUP:
        if (!same) {
            frame(GFX_BLUE);
            icon_card(GFX_BLUE);
            text(118, "НАЛАШТУВАННЯ", 2, GFX_WHITE);
            if (c->value != 0U) {
                text(146, "Прикладіть картку", 2, GFX_WHITE);
                text(168, "адміністратора", 2, GFX_WHITE);
            } else {
                text(146, "Натисніть", 2, GFX_GREY);
                text(168, "кнопку виходу", 2, GFX_GREY);
            }
        }
        break;

    case SCR_MENU:
        if (!same) {
            frame(GFX_BLUE);
            text(28, "МЕНЮ", 2, GFX_WHITE);
        }
        draw_menu_rows(c);
        break;

    case SCR_ENROLL:
        if (!same) {
            frame(GFX_BLUE);
            icon_card(GFX_BLUE);
            text(118, (c->value != 0U) ? "ДОДАТИ АДМІНА" : "ДОДАТИ КАРТКУ", 2, GFX_WHITE);
            text(146, "Прикладіть нову", 2, GFX_GREY);
            text(168, "картку", 2, GFX_GREY);
            text(200, "< назад", 1, GFX_GREY);
        }
        break;

    case SCR_DELETE:
        if (!same) {
            frame(GFX_RED);
            text(32, "ВИДАЛИТИ", 2, GFX_RED);
            text(176, "клік - видалити", 1, GFX_GREY);
            text(190, "< назад", 1, GFX_GREY);
        }
        draw_delete_dynamic(c);
        break;

    case SCR_LOG:
        if (!same) {
            frame(GFX_BLUE);
            text(32, "ЖУРНАЛ", 2, GFX_WHITE);
            text(184, "^ v гортати   < назад", 1, GFX_GREY);
        }
        draw_log_dynamic(c);
        break;

    case SCR_SETTINGS:
        if (!same) {
            frame(GFX_BLUE);
            text(50, "ЧАС ВІДКРИТТЯ", 2, GFX_WHITE);
            text(160, "клік - зберегти", 1, GFX_GREY);
            text(176, "< назад", 1, GFX_GREY);
        }
        draw_settings_dynamic(c);
        break;

    case SCR_RESULT_OK:
        if (!same) {
            frame(GFX_GREEN);
            icon_check(GFX_GREEN);
            text(126, "ГОТОВО", 3, GFX_GREEN);
            text(164, result_text(c->result), 2, GFX_WHITE);
        }
        break;

    case SCR_RESULT_FAIL:
        if (!same) {
            frame(GFX_RED);
            icon_cross(GFX_RED);
            text(126, "ПОМИЛКА", 3, GFX_RED);
            text(164, result_text(c->result), 2, GFX_WHITE);
        }
        break;

    default:
        break;
    }
}
