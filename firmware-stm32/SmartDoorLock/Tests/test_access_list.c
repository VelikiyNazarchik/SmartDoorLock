#include "test.h"
#include "access_list.h"
#include "crc32.h"
#include "fake_storage.h"

static AccessList al;

static const uint8_t UID_A[4] = { 0x04, 0xA1, 0xB2, 0xC3 };
static const uint8_t UID_B[7] = { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const uint8_t UID_C[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

static void fresh(void)
{
    fake_storage_reset();
    AL_Init(&al);
}

static void test_crc32_reference_value(void)
{
    CHECK_EQ(CRC32_Update(0, "123456789", 9), 0xCBF43926UL);
    uint32_t crc = CRC32_Update(0, "1234", 4);
    CHECK_EQ(CRC32_Update(crc, "56789", 5), 0xCBF43926UL);
}

static void test_empty_flash_means_first_start(void)
{
    fresh();
    CHECK_EQ(AL_Load(&al), AL_EMPTY);
    CHECK_EQ(al.count, 0);
    CHECK_EQ(al.unlock_time_s, CFG_UNLOCK_TIME_DEFAULT_S);
}

static void test_add_and_find(void)
{
    fresh();
    CHECK_EQ(AL_Add(&al, UID_A, 4, true), AL_OK);
    CHECK_EQ(AL_Add(&al, UID_B, 7, false), AL_OK);
    CHECK_EQ(al.count, 2);
    CHECK_EQ(AL_AdminCount(&al), 1);
    CHECK_EQ(AL_Find(&al, UID_B, 7), 1);
    CHECK_EQ(AL_Find(&al, UID_C, 4), -1);
    CHECK_EQ(AL_Find(&al, UID_B, 4), -1);
    CHECK_EQ(AL_Classify(&al, UID_A, 4), CARD_ADMIN);
    CHECK_EQ(AL_Classify(&al, UID_B, 7), CARD_USER);
    CHECK_EQ(AL_Classify(&al, UID_C, 4), CARD_UNKNOWN);
    CHECK(strcmp(al.cards[0].name, "Admin 01") == 0);
    CHECK(strcmp(al.cards[1].name, "Card 01") == 0);
    CHECK_EQ(AL_Add(&al, UID_B, 7, false), AL_ERR_EXISTS);
    CHECK_EQ(AL_Add(&al, UID_C, 0, false), AL_ERR_PARAM);
    CHECK_EQ(AL_Add(&al, UID_C, 8, false), AL_ERR_PARAM);
}

static void test_name_is_clean_after_terminator(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    for (size_t i = strlen(al.cards[0].name); i < CFG_CARD_NAME_LEN; i++) {
        CHECK_EQ(al.cards[0].name[i], 0);
    }
}

static void test_free_name_numbers_are_reused(void)
{
    fresh();
    uint8_t uid[4] = { 1, 2, 3, 0 };
    (void)AL_Add(&al, UID_A, 4, true);
    for (uint8_t i = 0; i < 3; i++) {
        uid[3] = i;
        (void)AL_Add(&al, uid, 4, false);
    }
    CHECK_EQ(AL_RemoveAt(&al, 2), AL_OK);
    uid[3] = 9;
    (void)AL_Add(&al, uid, 4, false);
    CHECK(strcmp(al.cards[3].name, "Card 02") == 0);
}

static void test_list_full(void)
{
    fresh();
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

static void test_last_admin_cannot_be_removed(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Add(&al, UID_B, 7, false);
    CHECK_EQ(AL_RemoveAt(&al, 0), AL_ERR_LAST_ADMIN);
    CHECK_EQ(AL_RemoveAt(&al, 5), AL_ERR_NOT_FOUND);
    CHECK_EQ(AL_RemoveAt(&al, 1), AL_OK);
    CHECK_EQ(al.count, 1);

    (void)AL_Add(&al, UID_C, 4, true);
    CHECK_EQ(AL_RemoveAt(&al, 0), AL_OK);
    CHECK_EQ(AL_Find(&al, UID_C, 4), 0);
}

static void test_save_and_load_back(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Add(&al, UID_B, 7, false);
    al.unlock_time_s = 12;
    CHECK_EQ(AL_Save(&al), AL_OK);

    AccessList loaded;
    CHECK_EQ(AL_Load(&loaded), AL_OK);
    CHECK_EQ(loaded.count, 2);
    CHECK_EQ(loaded.unlock_time_s, 12);
    CHECK_EQ(AL_Classify(&loaded, UID_B, 7), CARD_USER);
    CHECK(memcmp(loaded.cards, al.cards, 2 * sizeof(AL_Card)) == 0);

    CHECK_EQ(AL_RemoveAt(&al, 1), AL_OK);
    CHECK_EQ(AL_Save(&al), AL_OK);
    CHECK_EQ(AL_Load(&loaded), AL_OK);
    CHECK_EQ(loaded.count, 1);
}

static void test_flash_layout(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    CHECK_EQ(AL_Save(&al), AL_OK);
    CHECK(memcmp(fake_flash, "1KLD", 4) == 0);
    CHECK(memcmp(&fake_flash[sizeof(AL_Header)], UID_A, 4) == 0);
}

static void test_damaged_card_is_rejected(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Add(&al, UID_B, 7, false);
    CHECK_EQ(AL_Save(&al), AL_OK);
    fake_flash[sizeof(AL_Header) + sizeof(AL_Card) + 2] ^= 0x01;

    AccessList loaded;
    CHECK_EQ(AL_Load(&loaded), AL_ERR_CORRUPT);
    CHECK_EQ(loaded.count, 0);
}

static void test_damaged_header_is_rejected(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Save(&al);
    fake_flash[6] = 0xFF;
    CHECK_EQ(AL_Load(&al), AL_ERR_CORRUPT);

    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Save(&al);
    fake_flash[0] ^= 0x10;
    CHECK_EQ(AL_Load(&al), AL_ERR_CORRUPT);

    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    (void)AL_Save(&al);
    fake_flash[8] ^= 0x02;
    CHECK_EQ(AL_Load(&al), AL_ERR_CORRUPT);
}

static void test_power_loss_during_save(void)
{
    /* Живлення "зникає" після N байтів запису. Картки пишуться першими, заголовок останнім,
     * тому ні на якому N у Flash не лишається валідного, але неповного списку. */
    for (int budget = 0; budget < (int)(sizeof(AL_Header) + 2 * sizeof(AL_Card)); budget += 3) {
        fresh();
        (void)AL_Add(&al, UID_A, 4, true);
        (void)AL_Add(&al, UID_B, 7, false);
        fake_write_budget = budget;
        CHECK_EQ(AL_Save(&al), AL_ERR_STORAGE);
        fake_write_budget = -1;

        AccessList loaded;
        AL_Status st = AL_Load(&loaded);
        CHECK(st == AL_EMPTY || st == AL_ERR_CORRUPT);
        CHECK_EQ(loaded.count, 0);
    }
}

static void test_erase_error_is_reported(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    fake_erase_fails = true;
    CHECK_EQ(AL_Save(&al), AL_ERR_STORAGE);
}

static void test_bad_unlock_time_in_flash_uses_default(void)
{
    fresh();
    (void)AL_Add(&al, UID_A, 4, true);
    al.unlock_time_s = 0;
    CHECK_EQ(AL_Save(&al), AL_OK);
    AccessList loaded;
    CHECK_EQ(AL_Load(&loaded), AL_OK);
    CHECK_EQ(loaded.unlock_time_s, CFG_UNLOCK_TIME_DEFAULT_S);
}

int main(void)
{
    printf("test_access_list\n");
    RUN(test_crc32_reference_value);
    RUN(test_empty_flash_means_first_start);
    RUN(test_add_and_find);
    RUN(test_name_is_clean_after_terminator);
    RUN(test_free_name_numbers_are_reused);
    RUN(test_list_full);
    RUN(test_last_admin_cannot_be_removed);
    RUN(test_save_and_load_back);
    RUN(test_flash_layout);
    RUN(test_damaged_card_is_rejected);
    RUN(test_damaged_header_is_rejected);
    RUN(test_power_loss_during_save);
    RUN(test_erase_error_is_reported);
    RUN(test_bad_unlock_time_in_flash_uses_default);
    return TEST_REPORT();
}
