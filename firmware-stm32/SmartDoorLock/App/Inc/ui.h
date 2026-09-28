#ifndef UI_H
#define UI_H

#include <stdint.h>

/* What the display shows. The state machine picks the screen, tCtrl fills in the data,
 * tUi draws it: the UI never reads the card list or the log itself. */

#define UI_LINE_LEN 28U

typedef enum {
    SCR_BOOT = 0,
    SCR_LOCKED,
    SCR_GRANTED,
    SCR_DENIED,
    SCR_LOCKOUT,
    SCR_FAULT,
    SCR_SETUP,
    SCR_MENU,
    SCR_ENROLL,
    SCR_DELETE,
    SCR_LOG,
    SCR_SETTINGS,
    SCR_RESULT_OK,
    SCR_RESULT_FAIL,
    SCR_COUNT
} UiScreen;

typedef enum {
    RES_NONE = 0,
    RES_CARD_ADDED,
    RES_ADMIN_ADDED,
    RES_CARD_REMOVED,
    RES_SAVED,
    RES_ALREADY_EXISTS,
    RES_LIST_FULL,
    RES_LIST_EMPTY,
    RES_LAST_ADMIN,
    RES_STORAGE_ERROR,
    RES_RANDOM_UID          /* card changes its UID on every tap (phone, many bank cards) */
} UiResult;

typedef struct {
    UiScreen screen;
    uint8_t  sel;                 /* menu cursor / list position */
    uint8_t  count;               /* list length */
    uint16_t value;               /* seconds for settings/lockout */
    uint8_t  result;              /* UiResult */
    char     line1[UI_LINE_LEN];  /* free text: card name, log entry... */
    char     line2[UI_LINE_LEN];
} UiCommand;

/* Implemented in ui_screens.c (tUi). prev = what is on the panel now, NULL after power-up:
 * static parts are redrawn only when the screen changes, lists/values update in place. */
void UI_Draw(const UiCommand *cmd, const UiCommand *prev);

#endif
