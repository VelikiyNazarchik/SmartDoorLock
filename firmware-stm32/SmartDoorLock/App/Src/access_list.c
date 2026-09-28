#include <stdio.h>
#include <string.h>
#include "access_list.h"
#include "crc32.h"

_Static_assert(sizeof(AL_Card) == 24U, "AL_Card must stay a whole number of Flash words");
_Static_assert(sizeof(AL_Header) == 16U, "AL_Header layout changed");
_Static_assert(sizeof(AL_Header) + CFG_MAX_CARDS * sizeof(AL_Card) <= CFG_STORAGE_SIZE, "list does not fit");

#define AL_RECORDS_OFFSET ((uint32_t)sizeof(AL_Header))
#define AL_CRC_FIELDS_OFFSET 4U                      /* CRC starts at 'version' */
#define AL_CRC_FIELDS_LEN    8U                      /* version, count, unlock_time_s, reserved */

static uint16_t clamp_unlock(uint16_t s)
{
    if (s < CFG_UNLOCK_TIME_MIN_S || s > CFG_UNLOCK_TIME_MAX_S) {
        return CFG_UNLOCK_TIME_DEFAULT_S;
    }
    return s;
}

static uint32_t image_crc(const AL_Header *h, const AL_Card *cards)
{
    uint32_t crc = CRC32_Update(0, (const uint8_t *)h + AL_CRC_FIELDS_OFFSET, AL_CRC_FIELDS_LEN);
    return CRC32_Update(crc, cards, (size_t)h->count * sizeof(AL_Card));
}

static void reset_list(AccessList *al)
{
    memset(al->cards, 0, sizeof(al->cards));
    al->count = 0;
    al->unlock_time_s = CFG_UNLOCK_TIME_DEFAULT_S;
}

void AL_Init(AccessList *al, const AL_Storage *storage)
{
    al->storage = storage;
    reset_list(al);
}

AL_Status AL_Load(AccessList *al)
{
    AL_Header h;
    if (!al->storage->read(0, &h, sizeof(h))) {
        reset_list(al);
        return AL_ERR_STORAGE;
    }
    if (h.magic == 0xFFFFFFFFUL) {
        reset_list(al);
        return AL_EMPTY;
    }
    if (h.magic != AL_MAGIC || h.version != AL_VERSION || h.count > CFG_MAX_CARDS) {
        reset_list(al);
        return AL_ERR_CORRUPT;
    }
    if (h.count > 0U &&
        !al->storage->read(AL_RECORDS_OFFSET, al->cards, (uint32_t)h.count * sizeof(AL_Card))) {
        reset_list(al);
        return AL_ERR_STORAGE;
    }
    if (image_crc(&h, al->cards) != h.crc32) {
        reset_list(al);
        return AL_ERR_CORRUPT;
    }
    al->count = h.count;
    al->unlock_time_s = clamp_unlock(h.unlock_time_s);
    return AL_OK;
}

static bool verify_image(const AccessList *al, const AL_Header *expected)
{
    AL_Header h;
    if (!al->storage->read(0, &h, sizeof(h)) || memcmp(&h, expected, sizeof(h)) != 0) {
        return false;
    }
    uint32_t crc = CRC32_Update(0, (const uint8_t *)&h + AL_CRC_FIELDS_OFFSET, AL_CRC_FIELDS_LEN);
    for (uint16_t i = 0; i < h.count; i++) {
        AL_Card c;
        if (!al->storage->read(AL_RECORDS_OFFSET + (uint32_t)i * sizeof(AL_Card), &c, sizeof(c))) {
            return false;
        }
        crc = CRC32_Update(crc, &c, sizeof(c));
    }
    return crc == h.crc32;
}

AL_Status AL_Save(const AccessList *al)
{
    AL_Header h = {
        .magic = AL_MAGIC,
        .version = AL_VERSION,
        .count = al->count,
        .unlock_time_s = al->unlock_time_s,
        .reserved = 0xFFFFU,
        .crc32 = 0,
    };
    h.crc32 = image_crc(&h, al->cards);

    if (!al->storage->erase()) {
        return AL_ERR_STORAGE;
    }
    if (al->count > 0U &&
        !al->storage->write(AL_RECORDS_OFFSET, al->cards, (uint32_t)al->count * sizeof(AL_Card))) {
        return AL_ERR_STORAGE;
    }
    if (!al->storage->write(0, &h, sizeof(h))) {
        return AL_ERR_STORAGE;
    }
    return verify_image(al, &h) ? AL_OK : AL_ERR_STORAGE;
}

int AL_Find(const AccessList *al, const uint8_t *uid, uint8_t len)
{
    for (uint16_t i = 0; i < al->count; i++) {
        const AL_Card *c = &al->cards[i];
        if ((c->flags & AL_FLAG_ACTIVE) && c->uid_len == len && memcmp(c->uid, uid, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

CardClass AL_Classify(const AccessList *al, const uint8_t *uid, uint8_t len)
{
    int i = AL_Find(al, uid, len);
    if (i < 0) {
        return CARD_UNKNOWN;
    }
    return (al->cards[i].flags & AL_FLAG_ADMIN) ? CARD_ADMIN : CARD_USER;
}

AL_Status AL_Add(AccessList *al, const uint8_t *uid, uint8_t len, bool admin)
{
    if (uid == NULL || len == 0U || len > UID_MAX_LEN) {
        return AL_ERR_PARAM;
    }
    if (AL_Find(al, uid, len) >= 0) {
        return AL_ERR_EXISTS;
    }
    if (al->count >= CFG_MAX_CARDS) {
        return AL_ERR_FULL;
    }

    /* Name it after the lowest free number so names stay short and stable after deletions. */
    const char *prefix = admin ? "Admin" : "Card";
    char name[CFG_CARD_NAME_LEN] = { 0 };
    for (unsigned n = 1; n <= 99U; n++) {
        (void)snprintf(name, sizeof(name), "%s %02u", prefix, n);
        bool taken = false;
        for (uint16_t i = 0; i < al->count && !taken; i++) {
            taken = (strncmp(al->cards[i].name, name, CFG_CARD_NAME_LEN) == 0);
        }
        if (!taken) {
            break;
        }
    }

    AL_Card *c = &al->cards[al->count];
    memset(c, 0, sizeof(*c));
    memcpy(c->uid, uid, len);
    c->uid_len = len;
    c->flags = (uint8_t)(AL_FLAG_ACTIVE | (admin ? AL_FLAG_ADMIN : 0U));
    memcpy(c->name, name, sizeof(c->name));
    memset(c->reserved, 0xFF, sizeof(c->reserved));
    al->count++;
    return AL_OK;
}

AL_Status AL_RemoveAt(AccessList *al, uint16_t idx)
{
    if (idx >= al->count) {
        return AL_ERR_NOT_FOUND;
    }
    if ((al->cards[idx].flags & AL_FLAG_ADMIN) && AL_AdminCount(al) <= 1U) {
        return AL_ERR_LAST_ADMIN;
    }
    for (uint16_t i = idx; i + 1U < al->count; i++) {
        al->cards[i] = al->cards[i + 1U];
    }
    al->count--;
    memset(&al->cards[al->count], 0, sizeof(AL_Card));
    return AL_OK;
}

uint16_t AL_Count(const AccessList *al)
{
    return al->count;
}

uint16_t AL_AdminCount(const AccessList *al)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < al->count; i++) {
        if (al->cards[i].flags & AL_FLAG_ADMIN) {
            n++;
        }
    }
    return n;
}

const AL_Card *AL_Get(const AccessList *al, uint16_t idx)
{
    return (idx < al->count) ? &al->cards[idx] : NULL;
}

void AL_SetUnlockTime(AccessList *al, uint16_t seconds)
{
    al->unlock_time_s = clamp_unlock(seconds);
}

const char *AL_StatusStr(AL_Status st)
{
    switch (st) {
    case AL_OK:             return "OK";
    case AL_EMPTY:          return "EMPTY";
    case AL_ERR_CORRUPT:    return "CORRUPT";
    case AL_ERR_STORAGE:    return "STORAGE";
    case AL_ERR_FULL:       return "FULL";
    case AL_ERR_EXISTS:     return "EXISTS";
    case AL_ERR_NOT_FOUND:  return "NOT_FOUND";
    case AL_ERR_LAST_ADMIN: return "LAST_ADMIN";
    case AL_ERR_PARAM:      return "PARAM";
    default:                return "?";
    }
}
