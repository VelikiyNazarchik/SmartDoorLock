#ifndef ACCESS_IO_H
#define ACCESS_IO_H

#include <stdbool.h>
#include <stdint.h>
#include "buzzer_pattern.h"
#include "event_log.h"

/* Усе, що стан-машина може зробити із зовнішнім світом.
 * На платі ці функції реалізує tCtrl (task_ctrl.c): справжній замок, зумер, таймер,
 * дисплей і Flash. У тестах на ПК (Tests/test_access_ctrl.c) вони лише запам'ятовують,
 * що їх викликали, тож логіку можна перевірити без заліза. */

void IO_Lock(bool open);
void IO_Sound(BUZZ_Pattern pattern);
void IO_Timer(uint32_t ms);
void IO_Screen(void);
void IO_Log(LogEventType type, const uint8_t *uid, uint8_t uid_len);
bool IO_SaveCards(void);

#endif
