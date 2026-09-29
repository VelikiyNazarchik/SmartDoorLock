#include <stdio.h>
#include <string.h>
#include "access_list.h"
#include "crc32.h"
#include "storage.h"

_Static_assert(sizeof(AL_Card) == 24U, "запис картки має бути цілим числом слів Flash");
_Static_assert(sizeof(AL_Header) == 16U, "змінився формат заголовка");

#define CARDS_OFFSET  ((uint32_t)sizeof(AL_Header))

/* CRC рахується від поля version (пропускаємо magic) до кінця останньої картки. */
static uint32_t list_crc(const AL_Header *h, const AL_Card *cards)
{
    uint32_t crc = CRC32_Update(0, &h->version, 8);
    return CRC32_Update(crc, cards, (size_t)h->count * sizeof(AL_Card));
}

static uint16_t valid_unlock_time(uint16_t s)
{
    bool ok = (s >= CFG_UNLOCK_TIME_MIN_S && s <= CFG_UNLOCK_TIME_MAX_S);
    return ok ? s : CFG_UNLOCK_TIME_DEFAULT_S;
}

void AL_Init(AccessList *al)
{
    memset(al, 0, sizeof(*al));
    al->unlock_time_s = CFG_UNLOCK_TIME_DEFAULT_S;
}

AL_Status AL_Load(AccessList *al)
{
    AL_Header h;
    AL_Init(al);

    if (!STORE_Read(0, &h, sizeof(h))) {
        return AL_ERR_STORAGE;
    }
    if (h.magic == 0xFFFFFFFFUL) {
        return AL_EMPTY;
    }
    if (h.magic != AL_MAGIC || h.version != AL_VERSION || h.count > CFG_MAX_CARDS) {
        return AL_ERR_CORRUPT;
    }
    if (!STORE_Read(CARDS_OFFSET, al->cards, (uint32_t)h.count * sizeof(AL_Card))) {
        AL_Init(al);
        return AL_ERR_STORAGE;
    }
    if (list_crc(&h, al->cards) != h.crc32) {
        AL_Init(al);
        return AL_ERR_CORRUPT;
    }
    al->count = h.count;
    al->unlock_time_s = valid_unlock_time(h.unlock_time_s);
    return AL_OK;
}

AL_Status AL_Save(const AccessList *al)
{
    AL_Header h = {
        .magic = AL_MAGIC,
        .version = AL_VERSION,
        .count = al->count,
        .unlock_time_s = al->unlock_time_s,
        .reserved = 0xFFFFU,
    };
    h.crc32 = list_crc(&h, al->cards);

    /* Порядок важливий: картки спочатку, заголовок останнім (див. access_list.h). */
    if (!STORE_Erase()) {
        return AL_ERR_STORAGE;
    }
    if (!STORE_Write(CARDS_OFFSET, al->cards, (uint32_t)al->count * sizeof(AL_Card))) {
        return AL_ERR_STORAGE;
    }
    if (!STORE_Write(0, &h, sizeof(h))) {
        return AL_ERR_STORAGE;
    }

    AL_Header check;
    if (!STORE_Read(0, &check, sizeof(check)) || memcmp(&check, &h, sizeof(h)) != 0) {
        return AL_ERR_STORAGE;
    }
    return AL_OK;
}

int AL_Find(const AccessList *al, const uint8_t *uid, uint8_t len)
{
    for (uint16_t i = 0; i < al->count; i++) {
        const AL_Card *c = &al->cards[i];
        if (c->uid_len == len && memcmp(c->uid, uid, len) == 0) {
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

static void make_name(const AccessList *al, bool admin, char *name)
{
    for (unsigned n = 1; n <= 99U; n++) {
        (void)snprintf(name, CFG_CARD_NAME_LEN, "%s %02u", admin ? "Admin" : "Card", n);
        bool taken = false;
        for (uint16_t i = 0; i < al->count; i++) {
            if (strncmp(al->cards[i].name, name, CFG_CARD_NAME_LEN) == 0) {
                taken = true;
            }
        }
        if (!taken) {
            return;
        }
    }
}

AL_Status AL_Add(AccessList *al, const uint8_t *uid, uint8_t len, bool admin)
{
    if (len == 0U || len > UID_MAX_LEN) {
        return AL_ERR_PARAM;
    }
    if (AL_Find(al, uid, len) >= 0) {
        return AL_ERR_EXISTS;
    }
    if (al->count >= CFG_MAX_CARDS) {
        return AL_ERR_FULL;
    }

    AL_Card *c = &al->cards[al->count];
    memset(c, 0, sizeof(*c));
    memcpy(c->uid, uid, len);
    c->uid_len = len;
    c->flags = admin ? (AL_FLAG_ACTIVE | AL_FLAG_ADMIN) : AL_FLAG_ACTIVE;
    make_name(al, admin, c->name);
    al->count++;
    return AL_OK;
}

AL_Status AL_RemoveAt(AccessList *al, uint16_t index)
{
    if (index >= al->count) {
        return AL_ERR_NOT_FOUND;
    }
    bool is_admin = (al->cards[index].flags & AL_FLAG_ADMIN) != 0U;
    if (is_admin && AL_AdminCount(al) == 1U) {
        return AL_ERR_LAST_ADMIN;
    }
    for (uint16_t i = index; i + 1U < al->count; i++) {
        al->cards[i] = al->cards[i + 1U];
    }
    al->count--;
    memset(&al->cards[al->count], 0, sizeof(AL_Card));
    return AL_OK;
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
