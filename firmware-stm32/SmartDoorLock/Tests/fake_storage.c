#include <string.h>
#include "fake_storage.h"
#include "storage.h"

uint8_t fake_flash[FAKE_FLASH_SIZE];
int     fake_write_budget = -1;
bool    fake_erase_fails;

void fake_storage_reset(void)
{
    memset(fake_flash, 0xFF, sizeof(fake_flash));
    fake_write_budget = -1;
    fake_erase_fails = false;
}

bool STORE_IsSafe(void)
{
    return true;
}

bool STORE_Read(uint32_t offset, void *dst, uint32_t len)
{
    if (offset + len > FAKE_FLASH_SIZE) {
        return false;
    }
    memcpy(dst, &fake_flash[offset], len);
    return true;
}

bool STORE_Erase(void)
{
    if (fake_erase_fails) {
        return false;
    }
    memset(fake_flash, 0xFF, sizeof(fake_flash));
    return true;
}

bool STORE_Write(uint32_t offset, const void *src, uint32_t len)
{
    const uint8_t *bytes = src;
    if (offset + len > FAKE_FLASH_SIZE || len % 4U != 0U) {
        return false;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (fake_write_budget == 0) {
            return false;
        }
        if (fake_write_budget > 0) {
            fake_write_budget--;
        }
        fake_flash[offset + i] &= bytes[i];
    }
    return true;
}
