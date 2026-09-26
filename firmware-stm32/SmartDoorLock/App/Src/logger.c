#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "logger.h"

#define CONSOLE_RX_SIZE 128U   /* power of two */

static osMessageQueueId_t s_queue;
static volatile uint32_t  s_dropped;

static uint8_t           s_rx[CONSOLE_RX_SIZE];
static volatile uint32_t s_rx_head;   /* written by the USB interrupt */
static volatile uint32_t s_rx_tail;   /* read by tLog */

static bool in_isr(void)
{
    return __get_IPSR() != 0U;
}

void LOG_Init(osMessageQueueId_t queue)
{
    s_queue = queue;
}

static void post(LogMsg *m)
{
    if (s_queue == NULL || osMessageQueuePut(s_queue, m, 0, 0) != osOK) {
        s_dropped++;
    }
}

static void finish_line(LogMsg *m, int used)
{
    size_t n = (used < 0) ? 0U : (size_t)used;
    if (n > sizeof(m->text) - 3U) {
        n = sizeof(m->text) - 3U;   /* truncated: keep room for CRLF */
    }
    m->text[n++] = '\r';
    m->text[n++] = '\n';
    m->text[n] = '\0';
}

void LOG_Write(LogLevel lvl, const char *fmt, ...)
{
    static const char lvl_char[] = { 'D', 'I', 'W', 'E' };
    if (in_isr()) {
        s_dropped++;   /* formatting is too heavy for interrupt context */
        return;
    }

    LogMsg m;
    const char *task = "-";
    if (osKernelGetState() == osKernelRunning) {
        task = pcTaskGetName(NULL);
    }
    int n = snprintf(m.text, sizeof(m.text), "[%7lu] %c %-6s ",
                     (unsigned long)HAL_GetTick(), lvl_char[lvl & 3U], task);
    if (n < 0 || (size_t)n >= sizeof(m.text)) {
        n = 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(&m.text[n], sizeof(m.text) - (size_t)n, fmt, ap);
    va_end(ap);
    finish_line(&m, n + ((k < 0) ? 0 : k));
    post(&m);
}

void LOG_Raw(const char *fmt, ...)
{
    if (in_isr()) {
        s_dropped++;
        return;
    }
    LogMsg m;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(m.text, sizeof(m.text), fmt, ap);
    va_end(ap);
    finish_line(&m, k);
    post(&m);
}

uint32_t LOG_Dropped(void)
{
    return s_dropped;
}

void LOG_ConsoleRxFromISR(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (s_rx_head + 1U) & (CONSOLE_RX_SIZE - 1U);
        if (next == s_rx_tail) {
            return;   /* full: the rest of the packet is dropped */
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
    s_rx_tail = (s_rx_tail + 1U) & (CONSOLE_RX_SIZE - 1U);
    return true;
}
