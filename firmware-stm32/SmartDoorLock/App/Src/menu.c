#include "menu.h"

static const char *const labels[MENU_COUNT] = {
    [MENU_OPEN_DOOR]   = "Відчинити",
    [MENU_ADD_CARD]    = "Додати картку",
    [MENU_ADD_ADMIN]   = "Додати адміна",
    [MENU_DELETE_CARD] = "Видалити картку",
    [MENU_EVENT_LOG]   = "Журнал",
    [MENU_UNLOCK_TIME] = "Час відкриття",
    [MENU_EXIT]        = "Вихід",
};

const char *MENU_Label(MenuItem item)
{
    return (item < MENU_COUNT) ? labels[item] : "?";
}

uint8_t MENU_Move(uint8_t sel, uint8_t count, JOY_Direction dir)
{
    if (count == 0U) {
        return 0U;
    }
    if (sel >= count) {
        sel = (uint8_t)(count - 1U);
    }
    if (dir == JOY_DOWN) {
        return (uint8_t)((sel + 1U) % count);
    }
    if (dir == JOY_UP) {
        return (uint8_t)((sel == 0U) ? (count - 1U) : (sel - 1U));
    }
    return sel;
}
