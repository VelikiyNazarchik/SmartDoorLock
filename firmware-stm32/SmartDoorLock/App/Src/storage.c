#include <string.h>
#include "main.h"
#include "app_config.h"
#include "storage.h"
#include "supervisor.h"

/* Символи з лінкер-скрипту: образ .data лежить у Flash останнім, одразу після коду. */
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;

bool STORE_IsSafe(void)
{
    uint32_t firmware_end = (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
    return firmware_end <= CFG_STORAGE_ADDR;
}

static bool in_range(uint32_t offset, uint32_t len)
{
    return offset <= CFG_STORAGE_SIZE && len <= CFG_STORAGE_SIZE - offset;
}

bool STORE_Read(uint32_t offset, void *dst, uint32_t len)
{
    if (!in_range(offset, len)) {
        return false;
    }
    memcpy(dst, (const void *)(CFG_STORAGE_ADDR + offset), len);
    return true;
}

/* Після стирання/запису кеш даних Flash (ART) може віддавати старі байти — скидаємо його. */
static void reset_flash_cache(void)
{
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

bool STORE_Erase(void)
{
    if (!STORE_IsSafe()) {
        return false;
    }
    /* Стирання 128-КБ сектора зупиняє процесор до 2 с (код виконується з тієї ж Flash).
     * Watchdog налаштований на ~4 с, і перед стиранням ми його годуємо. */
    SUP_KickWatchdog();

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .Sector = CFG_STORAGE_SECTOR,
        .NbSectors = 1,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    };
    uint32_t bad_sector = 0;

    HAL_FLASH_Unlock();
    /* прапорці помилок від попередніх операцій інакше зірвуть і це стирання */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &bad_sector);
    HAL_FLASH_Lock();
    reset_flash_cache();
    return st == HAL_OK;
}

bool STORE_Write(uint32_t offset, const void *src, uint32_t len)
{
    if (!STORE_IsSafe() || !in_range(offset, len) || offset % 4U != 0U || len % 4U != 0U) {
        return false;
    }
    const uint8_t *bytes = src;
    HAL_StatusTypeDef st = HAL_OK;

    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < len && st == HAL_OK; i += 4U) {
        uint32_t word;
        memcpy(&word, &bytes[i], 4);
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CFG_STORAGE_ADDR + offset + i, word);
    }
    HAL_FLASH_Lock();
    reset_flash_cache();
    return st == HAL_OK;
}
