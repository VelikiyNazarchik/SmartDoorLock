#include "test.h"
#include "access_list.h"
#include "crc32.h"

/* RAM model of a Flash sector: erase sets 0xFF, program can only clear bits (like NOR Flash),
 * and a write budget simulates a power cut part-way through a save. */
#define FAKE_SIZE 4096U
static uint8_t flash[FAKE_SIZE];
static int     write_budget = -1;     /* bytes allowed before the "power cut"; -1 = unlimited */
static bool    fail_erase;

static bool fake_read(uint32_t off, void *dst, uint32_t len)
{
    if (off + len > FAKE_SIZE) {
        return false;
    }
    memcpy(dst, &flash[off], len);
    return true;
}

static bool fake_erase(void)
{
    if (fail_erase) {
        return false;
    }
    memset(flash, 0xFF, sizeof(flash));
    return true;
}

static bool fake_write(uint32_t off, const void *src, uint32_t len)
{
    const uint8_t *s = src;
    if (off + len > FAKE_SIZE || (len % 4U) != 0U) {
        return false;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (write_budget == 0) {
            return false;
        }
        if (write_budget > 0) {
            write_budget--;
        }
        flash[off + i] &= s[i];
    }
    return true;
}

static const AL_Storage fake = { fake_read, fake_erase, fake_write };
static AccessList al;

static const uint8_t UID_A[4] = { 0x04, 0xA1, 0xB2, 0xC3 };
static const uint8_t UID_B[7] = { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const uint8_t UID_C[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

static void reset_flash(void)
{
    memset(flash, 0xFF, sizeof(flash));
    write_budget = -1;
    fail_erase = false;
    AL_Init(&al, &fake);
}

static void test_crc32_reference_vector(void)
{
    CHECK_EQ(CRC32_Update(0, "123456789", 9), 0xCBF43926UL);
    uint32_t crc = CRC32_Update(0, "1234", 4);
    CHECK_EQ(CRC32_Update(crc, "56789", 5), 0xCBF43926UL);   /* chaining */
    CHECK_EQ(CRC32_Update(0, "", 0), 0);
}

static void test_erased_flash_is_empty(void)
{
    reset_flash();
    CHECK_EQ(AL_Load(&al), AL_EMPTY);
    CHECK_EQ(AL_Count(&al), 0);
    CHECK_EQ(al.unlock_time_s, CFG_UNLOCK_TIME_DEFAULT_S);
}

static void test_add_find_classify(void)
{
    reset_flash();
    CHECK_EQ(AL_Add(&al, UID_A, 4, true), AL_OK);
    CHECK_EQ(AL_Add(&al, UID_B, 7, false), AL_OK);
    CHECK_EQ(AL_Count(&al), 2);
    CHECK_EQ(AL_AdminCount(&al), 1);
    CHECK_EQ(AL_Find(&al, UID_B, 7), 1);
    CHECK_EQ(AL_Find(&al, UID_C, 4), -1);
    CHECK_EQ(AL_Find(&al, UID_B, 4), -1);            /* same prefix, other length */
    CHECK_EQ(AL_Classify(&al, UID_A, 4), CARD_ADMIN);
    CHECK_EQ(AL_Classify(&al, UID_B, 7), CARD_USER);
    CHECK_EQ(AL_Classify(&al, UID_C, 4), CARD_UNKNOWN);
    CHECK(strcmp(AL_Get(&al, 0)->name, "Admin 01") == 0);
    CHECK(strcmp(AL_Get(&al, 1)->name, "Card 01") == 0);
    CHECK_EQ(AL_Add(&al, UID_B, 7, false), AL_ERR_EXISTS);
    CHECK_EQ(AL_Add(&al, UID_C, 0, false), AL_ERR_PARAM);
    CHECK_EQ(AL_Add(&al, UID_C, 8, false), AL_ERR_PARAM);
}

static void test_names_reuse_free_numbers(void)
{
    reset_flash();
    uint8_t uid[4] = { 1, 2, 3, 0 };
    (void)AL_Add(&al, UID_A, 4, true);
    for (uint8_t i = 0; i < 3; i++) {
        uid[3] = i;
        (void)AL_Add(&al, uid, 4, false);
    }
    CHECK_EQ(AL_RemoveAt(&al, 2), AL_OK);            /* removes "Card 02" */
    uid[3] = 9;
    (void)AL_Add(&al, uid, 4, false);
    CHECK(strcmp(AL_Get(&al, 3)->name, "Card 02") == 0);
}

static void test_list_full(void)
{
    reset_flash();
    uint8_t uid[4] = { 0 };
    for (uint16_t i = 0; i < CFG_MAX_CARDS; i++) {
        uid[0] = (uint8_t)i;
        uid[1] = (uint8_t)(i >> 8);
        CHECK_EQ(AL_Add(&al, uid, 4, i == 0), AL_OK);
    }
    uid[0] = 0xEE;
    uid[1] = 0xEE;
    CHECK_EQ(AL_Add(&al, uid, 4, false), AL_ERR_FULL);
}

static void test_remove_and_last_admin_rule(void)
{
    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Add(&al, UID_B, 7, false);
    CHECK_EQ(AL_RemoveAt(&al, 0), AL_ERR_LAST_ADMIN);
    CHECK_EQ(AL_RemoveAt(&al, 5), AL_ERR_NOT_FOUND);
    CHECK_EQ(AL_RemoveAt(&al, 1), AL_OK);
    CHECK_EQ(AL_Count(&al), 1);

    (void)AL_Add(&al, UID_C, 4, true);                 /* second admin: now removable */
    CHECK_EQ(AL_RemoveAt(&al, 0), AL_OK);
    CHECK_EQ(AL_Find(&al, UID_C, 4), 0);
}

static void test_save_load_roundtrip(void)
{
    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Add(&al, UID_B, 7, false);
    AL_SetUnlockTime(&al, 12);
    CHECK_EQ(AL_Save(&al), AL_OK);

    AccessList other;
    AL_Init(&other, &fake);
    CHECK_EQ(AL_Load(&other), AL_OK);
    CHECK_EQ(AL_Count(&other), 2);
    CHECK_EQ(other.unlock_time_s, 12);
    CHECK_EQ(AL_Classify(&other, UID_B, 7), CARD_USER);
    CHECK(memcmp(other.cards, al.cards, 2 * sizeof(AL_Card)) == 0);

    /* a second save must fully replace the first image */
    CHECK_EQ(AL_RemoveAt(&al, 1), AL_OK);
    CHECK_EQ(AL_Save(&al), AL_OK);
    CHECK_EQ(AL_Load(&other), AL_OK);
    CHECK_EQ(AL_Count(&other), 1);
}

static void test_header_layout(void)
{
    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    CHECK_EQ(AL_Save(&al), AL_OK);
    uint32_t magic;
    memcpy(&magic, flash, 4);
    CHECK_EQ(magic, 0x444C4B31UL);                   /* "DLK1" little-endian */
    CHECK(memcmp(&flash[sizeof(AL_Header)], UID_A, 4) == 0);
}

static void test_corrupted_record_rejected(void)
{
    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Add(&al, UID_B, 7, false);
    CHECK_EQ(AL_Save(&al), AL_OK);
    flash[sizeof(AL_Header) + sizeof(AL_Card) + 2] ^= 0x01;   /* one flipped bit in record 1 */

    AccessList other;
    AL_Init(&other, &fake);
    CHECK_EQ(AL_Load(&other), AL_ERR_CORRUPT);
    CHECK_EQ(AL_Count(&other), 0);                   /* nothing from a bad image is trusted */
}

static void test_corrupted_header_rejected(void)
{
    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    CHECK_EQ(AL_Save(&al), AL_OK);

    flash[6] = 0xFF;                                  /* count field -> 0x..FF > max */
    CHECK_EQ(AL_Load(&al), AL_ERR_CORRUPT);

    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Save(&al);
    flash[0] ^= 0x10;                                 /* magic */
    CHECK_EQ(AL_Load(&al), AL_ERR_CORRUPT);

    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Save(&al);
    flash[8] ^= 0x02;                                 /* unlock time: covered by the CRC too */
    CHECK_EQ(AL_Load(&al), AL_ERR_CORRUPT);
}

static void test_power_cut_during_save(void)
{
    /* Records are written before the header: a cut at any byte leaves no valid image. */
    for (int budget = 0; budget < (int)(sizeof(AL_Header) + 2 * sizeof(AL_Card)); budget += 3) {
        reset_flash();
        (void)AL_Add(&al, UID_A, 4, true);
        (void)AL_Add(&al, UID_B, 7, false);
        write_budget = budget;
        CHECK_EQ(AL_Save(&al), AL_ERR_STORAGE);
        write_budget = -1;

        AccessList other;
        AL_Init(&other, &fake);
        AL_Status st = AL_Load(&other);
        CHECK(st == AL_EMPTY || st == AL_ERR_CORRUPT);
        CHECK_EQ(AL_Count(&other), 0);
    }
}

static void test_erase_failure_reported(void)
{
    reset_flash();
    (void)AL_Add(&al, UID_A, 4, true);
    fail_erase = true;
    CHECK_EQ(AL_Save(&al), AL_ERR_STORAGE);
}

static void test_unlock_time_clamped(void)
{
    reset_flash();
    AL_SetUnlockTime(&al, 0);
    CHECK_EQ(al.unlock_time_s, CFG_UNLOCK_TIME_DEFAULT_S);
    AL_SetUnlockTime(&al, CFG_UNLOCK_TIME_MAX_S + 1U);
    CHECK_EQ(al.unlock_time_s, CFG_UNLOCK_TIME_DEFAULT_S);
    AL_SetUnlockTime(&al, CFG_UNLOCK_TIME_MAX_S);
    CHECK_EQ(al.unlock_time_s, CFG_UNLOCK_TIME_MAX_S);
}

int main(void)
{
    printf("test_access_list\n");
    RUN(test_crc32_reference_vector);
    RUN(test_erased_flash_is_empty);
    RUN(test_add_find_classify);
    RUN(test_names_reuse_free_numbers);
    RUN(test_list_full);
    RUN(test_remove_and_last_admin_rule);
    RUN(test_save_load_roundtrip);
    RUN(test_header_layout);
    RUN(test_corrupted_record_rejected);
    RUN(test_corrupted_header_rejected);
    RUN(test_power_cut_during_save);
    RUN(test_erase_failure_reported);
    RUN(test_unlock_time_clamped);
    return TEST_REPORT();
}
