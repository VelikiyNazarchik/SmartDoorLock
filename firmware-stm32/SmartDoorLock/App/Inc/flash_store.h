#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdbool.h>
#include "access_list.h"

/* AL_Storage backend on the last 128 KB sector of the internal Flash (see app_config.h).
 * Owned by tCtrl. A sector erase stalls the whole CPU for up to ~2 s (RM0383, t_ERASE128KB),
 * so before_erase is called first to feed the watchdog. */

const AL_Storage *FLASH_STORE_Get(void);
void              FLASH_STORE_SetBeforeErase(void (*cb)(void));
/* False if the firmware image reaches into the storage sector (linker script not adjusted):
 * erasing it would destroy the running program, so all writes are refused. */
bool              FLASH_STORE_IsSafe(void);

#endif
