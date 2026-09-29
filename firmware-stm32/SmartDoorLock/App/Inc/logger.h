#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "app_config.h"

/* Лог через USB. Будь-яка задача форматує рядок і кладе його в чергу qLog, не чекаючи.
 * USB (CDC) чіпає лише задача tLog. Якщо черга повна — рядок відкидається і рахується,
 * бо логування ніколи не повинно гальмувати роботу замка. */

typedef struct {
    char text[CFG_LOG_MSG_LEN];
} LogMsg;

void     LOG_Init(osMessageQueueId_t queue);
void     LOG_Write(char level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void     LOG_Raw(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t LOG_Dropped(void);

#define LOG_DEBUG(...) LOG_Write('D', __VA_ARGS__)
#define LOG_INFO(...)  LOG_Write('I', __VA_ARGS__)
#define LOG_WARN(...)  LOG_Write('W', __VA_ARGS__)
#define LOG_ERR(...)   LOG_Write('E', __VA_ARGS__)

void     LOG_ConsoleRxFromISR(const uint8_t *buf, uint32_t len);
bool     LOG_ConsoleGetChar(char *c);

#endif
