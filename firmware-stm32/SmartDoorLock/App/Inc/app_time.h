#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdbool.h>
#include <stdint.h>

/* Годинник (RTC) для міток часу в журналі + backup-регістри RTC.
 *
 * Налаштований регістрами, а не в CubeMX: згенерований код запускає кварц 32 кГц разом із
 * основним і назавжди зависає в Error_Handler(), якщо той не завівся. Тут — очікування з
 * таймаутом і перехід на внутрішній LSI. Без батарейки на VBAT час губиться при вимкненні. */

typedef enum {
    TIME_SRC_NONE = 0,
    TIME_SRC_LSE,
    TIME_SRC_LSI
} TimeSource;

TimeSource  TIME_Init(void (*wait_ms)(uint32_t ms), void (*kick)(void));
TimeSource  TIME_Source(void);
const char *TIME_SourceName(void);
bool        TIME_IsSet(void);
uint32_t    TIME_Now(void);
bool        TIME_Set(uint32_t epoch);

uint32_t    TIME_BkpRead(uint32_t idx);
void        TIME_BkpWrite(uint32_t idx, uint32_t value);

#endif
