#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "app_config.h"

/* Any task formats a line and drops it into qLog without blocking; tLog alone owns USB CDC.
 * A full queue drops the line and counts it instead of stalling the caller. */

typedef enum {
    LOG_LVL_DEBUG = 0,
    LOG_LVL_INFO,
    LOG_LVL_WARN,
    LOG_LVL_ERROR
} LogLevel;

typedef struct {
    char text[CFG_LOG_MSG_LEN];
} LogMsg;

void     LOG_Init(osMessageQueueId_t queue);
void     LOG_Write(LogLevel lvl, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Raw line without prefix (console output). */
void     LOG_Raw(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t LOG_Dropped(void);

/* Debug console input: USB RX interrupt -> ring buffer -> tLog. */
void     LOG_ConsoleRxFromISR(const uint8_t *buf, uint32_t len);
bool     LOG_ConsoleGetChar(char *c);

#define LOG_DEBUG(...) LOG_Write(LOG_LVL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  LOG_Write(LOG_LVL_INFO, __VA_ARGS__)
#define LOG_WARN(...)  LOG_Write(LOG_LVL_WARN, __VA_ARGS__)
#define LOG_ERR(...)   LOG_Write(LOG_LVL_ERROR, __VA_ARGS__)

#endif
