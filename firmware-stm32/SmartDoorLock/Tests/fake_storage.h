#ifndef FAKE_STORAGE_H
#define FAKE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Підробка Flash для тестів: масив у RAM, що поводиться як NOR-Flash
 * (стирання = 0xFF, запис може лише скидати біти), і можна імітувати збої. */

#define FAKE_FLASH_SIZE 4096U

extern uint8_t fake_flash[FAKE_FLASH_SIZE];
extern int     fake_write_budget;
extern bool    fake_erase_fails;

void fake_storage_reset(void);

#endif
