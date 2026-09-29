#ifndef ACCESS_LIST_H
#define ACCESS_LIST_H

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_events.h"

/* Список карток у RAM + збереження у Flash з контрольною сумою.
 *
 * Як лежить у Flash:
 *   зміщення 0x00: заголовок AL_Header { "DLK1", версія, кількість, час відкриття, CRC32 }
 *   зміщення 0x10: записи AL_Card[кількість]
 *
 * Порядок запису: стерти сектор -> записати картки -> заголовок ОСТАННІМ.
 * Якщо живлення зникне посеред запису, заголовка ще немає (або він неповний),
 * і при старті список просто вважається порожнім — "напівсписку" не буває. */

#define AL_MAGIC        0x444C4B31UL
#define AL_VERSION      1U
#define AL_FLAG_ACTIVE  0x01U
#define AL_FLAG_ADMIN   0x02U

typedef enum {
    CARD_UNKNOWN = 0,
    CARD_USER,
    CARD_ADMIN
} CardClass;

typedef struct {
    uint8_t uid[UID_MAX_LEN];
    uint8_t uid_len;
    uint8_t flags;
    char    name[CFG_CARD_NAME_LEN];
    uint8_t reserved[3];
} AL_Card;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t unlock_time_s;
    uint16_t reserved;
    uint32_t crc32;
} AL_Header;

typedef enum {
    AL_OK = 0,
    AL_EMPTY,
    AL_ERR_CORRUPT,
    AL_ERR_STORAGE,
    AL_ERR_FULL,
    AL_ERR_EXISTS,
    AL_ERR_NOT_FOUND,
    AL_ERR_LAST_ADMIN,    /* останнього адміна не видаляємо, інакше ніхто не зайде в меню */
    AL_ERR_PARAM
} AL_Status;

typedef struct {
    AL_Card  cards[CFG_MAX_CARDS];
    uint16_t count;
    uint16_t unlock_time_s;
} AccessList;

void        AL_Init(AccessList *al);
AL_Status   AL_Load(AccessList *al);
AL_Status   AL_Save(const AccessList *al);
int         AL_Find(const AccessList *al, const uint8_t *uid, uint8_t len);
CardClass   AL_Classify(const AccessList *al, const uint8_t *uid, uint8_t len);
AL_Status   AL_Add(AccessList *al, const uint8_t *uid, uint8_t len, bool admin);
AL_Status   AL_RemoveAt(AccessList *al, uint16_t index);
uint16_t    AL_AdminCount(const AccessList *al);
const char *AL_StatusStr(AL_Status st);

#endif
