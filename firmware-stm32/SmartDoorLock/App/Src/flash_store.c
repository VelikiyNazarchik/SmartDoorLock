#include <string.h>
#include "main.h"
#include "app_config.h"
#include "flash_store.h"

/* Linker symbols: .data is the last thing placed in FLASH (its load image follows .text/.rodata). */
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;

static void (*s_before_erase)(void);

bool FLASH_STORE_IsSafe(void)
{
    uint32_t image_end = (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
    return image_end <= CFG_STORAGE_ADDR;
}

static bool in_range(uint32_t off, uint32_t len)
{
    return off <= CFG_STORAGE_SIZE && len <= CFG_STORAGE_SIZE - off;
}

static bool st_read(uint32_t off, void *dst, uint32_t len)
{
    if (!in_range(off, len)) {
        return false;
    }
    memcpy(dst, (const void *)(CFG_STORAGE_ADDR + off), len);
    return true;
}

static void flush_data_cache(void)
{
    /* The ART data cache may still hold the pre-program contents (RM0383 3.4.2). */
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

static bool st_erase(void)
{
    if (!FLASH_STORE_IsSafe()) {
        return false;
    }
    if (s_before_erase != NULL) {
        s_before_erase();
    }
    FLASH_EraseInitTypeDef er = {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .Banks = FLASH_BANK_1,
        .Sector = CFG_STORAGE_SECTOR,
        .NbSectors = 1,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,   /* 2.7-3.6 V: x32 parallelism */
    };
    uint32_t bad_sector = 0;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&er, &bad_sector);
    HAL_FLASH_Lock();
    flush_data_cache();
    return st == HAL_OK;
}

static bool st_write(uint32_t off, const void *src, uint32_t len)
{
    if (!FLASH_STORE_IsSafe() || !in_range(off, len) || (off % 4U) != 0U || (len % 4U) != 0U) {
        return false;
    }
    const uint8_t *p = (const uint8_t *)src;
    HAL_StatusTypeDef st = HAL_OK;
    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < len && st == HAL_OK; i += 4U) {
        uint32_t word;
        memcpy(&word, &p[i], 4);
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CFG_STORAGE_ADDR + off + i, word);
    }
    HAL_FLASH_Lock();
    flush_data_cache();
    return st == HAL_OK;
}

static const AL_Storage s_storage = { st_read, st_erase, st_write };

const AL_Storage *FLASH_STORE_Get(void)
{
    return &s_storage;
}

void FLASH_STORE_SetBeforeErase(void (*cb)(void))
{
    s_before_erase = cb;
}
