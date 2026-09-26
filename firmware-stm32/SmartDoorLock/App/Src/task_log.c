#include <stdio.h>
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

/* tLog: the only user of USB CDC. Batches queued log lines into one USB transfer and runs a
 * small debug console on the same virtual COM port. */

#define USB_TX_BUF         512U
#define USB_WAIT_BOOT_MS   3000U   /* hold boot messages until the host enumerates the port */

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t s_tx[USB_TX_BUF];   /* owned by the USB core until TxState returns to 0 */
static char    s_line[CFG_CONSOLE_LINE_LEN];
static uint32_t s_line_len;
static bool    s_reboot_pending;

static bool usb_ready(void)
{
    return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData != NULL;
}

static bool usb_idle(void)
{
    USBD_CDC_HandleTypeDef *cdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    return cdc != NULL && cdc->TxState == 0U;
}

static bool wait_usb_idle(void)
{
    uint32_t t0 = osKernelGetTickCount();
    while (!usb_idle()) {
        if ((osKernelGetTickCount() - t0) > CFG_LOG_USB_TIMEOUT_MS) {
            return false;   /* nobody reads the port: drop instead of blocking */
        }
        osDelay(1);
    }
    return true;
}

/* Collects the first line (already received) plus whatever else is queued, then sends it in one go. */
static void flush_to_usb(const LogMsg *first)
{
    uint32_t n = 0;
    LogMsg m = *first;
    for (;;) {
        size_t len = strlen(m.text);
        if (n + len > sizeof(s_tx)) {
            break;   /* this line waits for the next transfer */
        }
        memcpy(&s_tx[n], m.text, len);
        n += (uint32_t)len;
        if (osMessageQueueGet(qLog, &m, NULL, 0) != osOK) {
            m.text[0] = '\0';
            break;
        }
    }
    if (n > 0U && usb_ready()) {
        (void)CDC_Transmit_FS(s_tx, (uint16_t)n);
    }
    if (m.text[0] != '\0') {
        /* did not fit: send it right after the current transfer */
        if (wait_usb_idle() && usb_ready()) {
            size_t len = strlen(m.text);
            memcpy(s_tx, m.text, len);
            (void)CDC_Transmit_FS(s_tx, (uint16_t)len);
        }
    }
}

static void post_console(ConsoleCmd cmd, uint32_t arg)
{
    AppEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_CONSOLE;
    ev.console.cmd = (uint8_t)cmd;
    ev.console.arg = arg;
    if (!App_PostCtrl(&ev)) {
        LOG_Raw("ctrl queue full, try again");
    }
}

static void console_exec(char *line)
{
    while (*line == ' ') {
        line++;
    }
    if (line[0] == '\0') {
        return;
    }
    if (strcmp(line, "help") == 0) {
        LOG_Raw("commands: help | stat | status | cards | log | time [YYYY-MM-DD HH:MM:SS]");
        LOG_Raw("          hang <tCtrl|tRfid|tInput|tUi|tNet|tLog> | reboot");
    } else if (strcmp(line, "stat") == 0) {
        LOG_Raw("reset cause: %s", SUP_ResetText());
        SUP_PrintStats();
    } else if (strcmp(line, "status") == 0) {
        post_console(CON_CMD_STATUS, 0);
    } else if (strcmp(line, "cards") == 0) {
        post_console(CON_CMD_CARDS, 0);
    } else if (strcmp(line, "log") == 0) {
        post_console(CON_CMD_LOG, 0);
    } else if (strcmp(line, "time") == 0) {
        post_console(CON_CMD_TIME_SET, 0);   /* arg 0 = just print */
    } else if (strncmp(line, "time ", 5) == 0) {
        TU_DateTime dt;
        if (TU_Parse(&line[5], &dt)) {
            post_console(CON_CMD_TIME_SET, TU_ToEpoch(&dt));
        } else {
            LOG_Raw("bad time, expected YYYY-MM-DD HH:MM:SS");
        }
    } else if (strncmp(line, "hang ", 5) == 0) {
        LOG_Raw(SUP_RequestHang(&line[5]) ? "ok: %s will stop sending heartbeats" : "no task %s", &line[5]);
    } else if (strcmp(line, "reboot") == 0) {
        LOG_Raw("rebooting...");
        s_reboot_pending = true;   /* after this line has been sent */
    } else {
        LOG_Raw("unknown command '%s', try help", line);
    }
}

static void console_poll(void)
{
    char c;
    while (LOG_ConsoleGetChar(&c)) {
        if (c == '\r' || c == '\n') {
            if (s_line_len > 0U) {
                s_line[s_line_len] = '\0';
                console_exec(s_line);
                s_line_len = 0;
            }
        } else if (c == '\b' || c == 0x7F) {
            if (s_line_len > 0U) {
                s_line_len--;
            }
        } else if (s_line_len + 1U < sizeof(s_line) && c >= ' ') {
            s_line[s_line_len++] = c;
        }
    }
}

void TaskLog(void *arg)
{
    (void)arg;
    uint32_t t0 = osKernelGetTickCount();
    while (!usb_ready() && (osKernelGetTickCount() - t0) < USB_WAIT_BOOT_MS) {
        SUP_Heartbeat(TASK_LOG);
        osDelay(50);
    }
    osDelay(200);   /* give a terminal that auto-opens on enumeration a moment to attach */

    LogMsg m;
    for (;;) {
        SUP_Heartbeat(TASK_LOG);
        if (osMessageQueueGet(qLog, &m, NULL, 100) == osOK) {
            if (usb_ready() && wait_usb_idle()) {
                flush_to_usb(&m);
            }
            /* otherwise the line is dropped: USB unplugged or port not open */
        } else if (s_reboot_pending) {
            (void)wait_usb_idle();
            SUP_FatalReset(RESET_REASON_CONSOLE, NULL);
        }
        console_poll();
    }
}
