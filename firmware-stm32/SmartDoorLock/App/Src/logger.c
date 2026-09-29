#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "logger.h"

#define CONSOLE_RX_SIZE 128U

static osMessageQueueId_t s_queue;
static volatile uint32_t  s_dropped;

void LOG_Init(osMessageQueueId_t queue)
{
    s_queue = queue;
}

uint32_t LOG_Dropped(void)
{
    return s_dropped;
}

static void send_line(LogMsg *m, int len)
{
    size_t n = (len < 0) ? 0U : (size_t)len;
    if (n > sizeof(m->text) - 3U) {
        n = sizeof(m->text) - 3U;
    }
    strcpy(&m->text[n], "\r\n");

    if (s_queue == NULL || osMessageQueuePut(s_queue, m, 0, 0) != osOK) {
        s_dropped++;
    }
}

void LOG_Write(char level, const char *fmt, ...)
{
    if (__get_IPSR() != 0U) {
        s_dropped++;   /* у перериваннях не логуємо: форматування надто довге */
        return;
    }
    const char *task = (osKernelGetState() == osKernelRunning) ? pcTaskGetName(NULL) : "-";

    LogMsg m;
    int n = snprintf(m.text, sizeof(m.text), "[%7lu] %c %-6s ", (unsigned long)HAL_GetTick(), level, task);

    va_list args;
    va_start(args, fmt);
    n += vsnprintf(&m.text[n], sizeof(m.text) - (size_t)n, fmt, args);
    va_end(args);

    send_line(&m, n);
}

void LOG_Raw(const char *fmt, ...)
{
    if (__get_IPSR() != 0U) {
        s_dropped++;
        return;
    }
    LogMsg m;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(m.text, sizeof(m.text), fmt, args);
    va_end(args);
    send_line(&m, n);
}

static uint8_t           s_rx[CONSOLE_RX_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;

void LOG_ConsoleRxFromISR(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (s_rx_head + 1U) % CONSOLE_RX_SIZE;
        if (next == s_rx_tail) {
            return;
        }
        s_rx[s_rx_head] = buf[i];
        s_rx_head = next;
    }
}

bool LOG_ConsoleGetChar(char *c)
{
    if (s_rx_tail == s_rx_head) {
        return false;
    }
    *c = (char)s_rx[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1U) % CONSOLE_RX_SIZE;
    return true;
}
