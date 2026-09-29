#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stdint.h>

bool STORE_Read(uint32_t offset, void *dst, uint32_t len);
bool STORE_Erase(void);
bool STORE_Write(uint32_t offset, const void *src, uint32_t len);

/* false, якщо прошивка сама заходить у сектор даних (не змінили лінкер-скрипт):
 * тоді стирання знищило б програму, тому всі записи забороняються. */
bool STORE_IsSafe(void);

#endif
