#ifndef ACCESS_LIST_H
#define ACCESS_LIST_H

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_events.h"

/* Card list with a CRC-protected persistent image. HAL-free: the storage is reached through
 * AL_Storage, implemented by flash_store.c on the target and by a RAM fake in the PC tests.
 *
 * Image layout (offsets from the start of the storage area):
 *   0x00  AL_Header   { magic "DLK1", version, count, unlock_time_s, reserved, crc32 }
 *   0x10  AL_Card[count]
 * Write order: erase -> records -> header last. A power cut in the middle leaves an erased or
 * invalid header, never a header that points at half-written records. */

#define AL_MAGIC          0x444C4B31UL   /* "DLK1" */
#define AL_VERSION        1U

#define AL_FLAG_ACTIVE    0x01U
#define AL_FLAG_ADMIN     0x02U

typedef struct {
    uint8_t uid[UID_MAX_LEN];
    uint8_t uid_len;
    uint8_t flags;
    char    name[CFG_CARD_NAME_LEN];
    uint8_t reserved[3];
} AL_Card;                                /* 24 bytes: whole Flash words */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t unlock_time_s;
    uint16_t reserved;
    uint32_t crc32;                       /* over version..reserved + all records */
} AL_Header;                              /* 16 bytes */

typedef struct {
    bool (*read)(uint32_t offset, void *dst, uint32_t len);
    bool (*erase)(void);
    bool (*write)(uint32_t offset, const void *src, uint32_t len);   /* len is a multiple of 4 */
} AL_Storage;

typedef enum {
    AL_OK = 0,
    AL_EMPTY,               /* storage erased: first boot */
    AL_ERR_CORRUPT,         /* bad magic/version/count/CRC: list reset to empty */
    AL_ERR_STORAGE,         /* read/erase/program/verify failed */
    AL_ERR_FULL,
    AL_ERR_EXISTS,
    AL_ERR_NOT_FOUND,
    AL_ERR_LAST_ADMIN,      /* refusing to remove the only admin card */
    AL_ERR_PARAM
} AL_Status;

typedef struct {
    const AL_Storage *storage;
    AL_Card           cards[CFG_MAX_CARDS];
    uint16_t          count;
    uint16_t          unlock_time_s;
} AccessList;

void           AL_Init(AccessList *al, const AL_Storage *storage);
AL_Status      AL_Load(AccessList *al);
AL_Status      AL_Save(const AccessList *al);
int            AL_Find(const AccessList *al, const uint8_t *uid, uint8_t len);
CardClass      AL_Classify(const AccessList *al, const uint8_t *uid, uint8_t len);
AL_Status      AL_Add(AccessList *al, const uint8_t *uid, uint8_t len, bool admin);
AL_Status      AL_RemoveAt(AccessList *al, uint16_t idx);
uint16_t       AL_Count(const AccessList *al);
uint16_t       AL_AdminCount(const AccessList *al);
const AL_Card *AL_Get(const AccessList *al, uint16_t idx);
void           AL_SetUnlockTime(AccessList *al, uint16_t seconds);
const char    *AL_StatusStr(AL_Status st);

#endif
