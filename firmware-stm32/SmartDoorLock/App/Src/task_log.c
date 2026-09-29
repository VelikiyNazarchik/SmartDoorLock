#include <string.h>
#include "main.h"
#include "usb_device.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"
#include "timeutil.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t s_tx[CFG_LOG_MSG_LEN];      /* USB передає прямо з цього буфера, тому він статичний */
static char    s_line[CFG_CONSOLE_LINE_LEN];
static uint32_t s_line_len;

static bool usb_connected(void)
{
    return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData != NULL;
}

static bool usb_busy(void)
{
    USBD_CDC_HandleTypeDef *cdc = hUsbDeviceFS.pClassData;
    return cdc->TxState != 0U;
}

static void usb_send(const char *text)
{
    if (!usb_connected()) {
        return;
    }
    uint32_t start = osKernelGetTickCount();
    while (usb_busy()) {
        if (osKernelGetTickCount() - start > CFG_LOG_USB_TIMEOUT_MS) {
            return;
        }
        osDelay(1);
    }
    size_t len = strlen(text);
    memcpy(s_tx, text, len);
    (void)CDC_Transmit_FS(s_tx, (uint16_t)len);
}

static void ask_ctrl(ConsoleCmd cmd, uint32_t arg)
{
    AppEvent ev = { .type = EV_CONSOLE, .console_cmd = (uint8_t)cmd, .console_arg = arg };
    if (!App_PostCtrl(&ev)) {
        LOG_Raw("busy, try again");
    }
}

static void run_command(const char *cmd)
{
    if (strcmp(cmd, "help") == 0) {
        LOG_Raw("commands: help | stat | status | cards | log | time [YYYY-MM-DD HH:MM:SS]");
        LOG_Raw("          hang <tCtrl|tRfid|tInput|tUi|tNet|tLog> | reboot");
    } else if (strcmp(cmd, "stat") == 0) {
        LOG_Raw("reset cause: %s", SUP_ResetText());
        SUP_PrintStats();
    } else if (strcmp(cmd, "status") == 0) {
        ask_ctrl(CON_STATUS, 0);
    } else if (strcmp(cmd, "cards") == 0) {
        ask_ctrl(CON_CARDS, 0);
    } else if (strcmp(cmd, "log") == 0) {
        ask_ctrl(CON_LOG, 0);
    } else if (strcmp(cmd, "time") == 0) {
        ask_ctrl(CON_TIME, 0);
    } else if (strncmp(cmd, "time ", 5) == 0) {
        TU_DateTime dt;
        if (TU_Parse(&cmd[5], &dt)) {
            ask_ctrl(CON_TIME, TU_ToEpoch(&dt));
        } else {
            LOG_Raw("expected: time YYYY-MM-DD HH:MM:SS");
        }
    } else if (strncmp(cmd, "hang ", 5) == 0) {
        if (SUP_RequestHang(&cmd[5])) {
            LOG_Raw("ok: %s will stop sending heartbeats", &cmd[5]);
        } else {
            LOG_Raw("no task %s", &cmd[5]);
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        usb_send("rebooting...\r\n");
        osDelay(100);
        SUP_FatalReset(RESET_REASON_CONSOLE, NULL);
    } else {
        LOG_Raw("unknown command '%s', try help", cmd);
    }
}

static void read_console(void)
{
    char c;
    while (LOG_ConsoleGetChar(&c)) {
        if (c == '\r' || c == '\n') {
            if (s_line_len > 0U) {
                s_line[s_line_len] = '\0';
                run_command(s_line);
                s_line_len = 0;
            }
        } else if ((c == '\b' || c == 0x7F) && s_line_len > 0U) {
            s_line_len--;
        } else if (c >= ' ' && s_line_len + 1U < sizeof(s_line)) {
            s_line[s_line_len++] = c;
        }
    }
}

void TaskLog(void *arg)
{
    (void)arg;

    uint32_t start = osKernelGetTickCount();
    while (!usb_connected() && osKernelGetTickCount() - start < 3000U) {
        SUP_Heartbeat(TASK_LOG);
        osDelay(50);
    }

    LogMsg m;
    for (;;) {
        SUP_Heartbeat(TASK_LOG);
        if (osMessageQueueGet(qLog, &m, NULL, 100) == osOK) {
            usb_send(m.text);
        }
        read_console();
    }
}
