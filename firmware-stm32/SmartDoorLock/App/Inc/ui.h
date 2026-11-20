#ifndef UI_H
#define UI_H

#include <stdint.h>

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
    SCR_RECOVERY_CARD,
    SCR_RECOVERY_CODE,
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
    RES_RANDOM_UID,
    RES_ADMIN_REPLACED,
    RES_WRONG_CODE
} UiResult;

typedef struct {
    UiScreen screen;
    uint8_t  sel;
    uint8_t  count;
    uint16_t value;
    uint8_t  result;
    char     line1[UI_LINE_LEN];
    char     line2[UI_LINE_LEN];
} UiCommand;

void UI_Draw(const UiCommand *cmd, const UiCommand *prev);

#endif
