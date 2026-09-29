#include "test.h"
#include "access_ctrl.h"
#include "access_io.h"
#include "menu.h"

/* Підробка IO_*: замість замка, зумера і дисплея — запис того, що стан-машина викликала. */

static struct {
    int          lock;
    BUZZ_Pattern sound;
    int          timer_ms;
    int          screens;
    int          logs;
    LogEventType log_type;
    uint8_t      log_uid0;
    int          saves;
    bool         save_ok;
} io;

static AccessCtx  c;
static AccessList cards;
static EventLog   events;

void IO_Lock(bool open)                { io.lock = open ? 1 : 0; }
void IO_Sound(BUZZ_Pattern pattern)    { io.sound = pattern; }
void IO_Timer(uint32_t ms)             { io.timer_ms = (int)ms; }
void IO_Screen(void)                   { io.screens++; }
bool IO_SaveCards(void)                { io.saves++; return io.save_ok; }

void IO_Log(LogEventType type, const uint8_t *uid, uint8_t uid_len)
{
    io.logs++;
    io.log_type = type;
    io.log_uid0 = (uid_len > 0U) ? uid[0] : 0U;
    ELOG_Add(&events, type, uid, uid_len, 0, false);
}

static void io_clear(void)
{
    io.lock = -1;
    io.sound = BUZZ_NONE;
    io.timer_ms = -1;
    io.screens = 0;
    io.logs = 0;
    io.log_type = LOGE_COUNT;
    io.log_uid0 = 0;
    io.saves = 0;
}

#define ADMIN    0xA0U
#define USER     0x70U
#define STRANGER 0x55U
#define RANDOM   0x08U

static AppEvent card(uint8_t first_byte)
{
    AppEvent e = { .type = EV_CARD, .uid = { first_byte, 0xA1, 0xB2, 0xC3 }, .uid_len = 4 };
    return e;
}

static AppEvent event(AppEventType type)
{
    AppEvent e = { .type = type };
    return e;
}

static AppEvent joy(JOY_Direction dir)
{
    AppEvent e = { .type = EV_JOY_DIR, .dir = dir };
    return e;
}

static CardClass class_of(uint8_t first_byte)
{
    AppEvent e = card(first_byte);
    return AL_Classify(&cards, e.uid, e.uid_len);
}

static void send(AppEvent e, uint32_t now)
{
    io_clear();
    AC_HandleEvent(&c, &e, now);
}

static void start(bool with_cards)
{
    AL_Init(&cards);
    ELOG_Init(&events);
    if (with_cards) {
        AppEvent a = card(ADMIN);
        AppEvent u = card(USER);
        (void)AL_Add(&cards, a.uid, a.uid_len, true);
        (void)AL_Add(&cards, u.uid, u.uid_len, false);
    }
    io.save_ok = true;
    AC_Init(&c, &cards, &events);
    io_clear();
    AC_Start(&c, 0);
}

static void start_locked(void)
{
    start(true);
}

static void start_setup_armed(void)
{
    start(false);
    send(event(EV_EXIT_BUTTON), 10);
    send(event(EV_TIMEOUT), 5010);
}

static void open_menu(uint32_t now)
{
    send(card(ADMIN), now);
}

static void choose(MenuItem item, uint32_t now)
{
    while (c.menu_pos != (uint8_t)item) {
        send(joy(JOY_DOWN), now);
    }
    send(event(EV_JOY_CLICK), now);
}

static void test_start_closes_the_lock(void)
{
    start_locked();
    CHECK_EQ(c.state, AC_LOCKED);
    CHECK_EQ(io.lock, 0);
    CHECK_EQ(io.timer_ms, 0);
    CHECK(io.screens > 0);
}

static void test_start_without_admin_is_setup(void)
{
    start(false);
    CHECK_EQ(c.state, AC_SETUP);
    CHECK_EQ(AC_Screen(&c), SCR_SETUP);
}

static void test_user_card_opens_then_closes(void)
{
    start_locked();
    send(card(USER), 1000);
    CHECK_EQ(c.state, AC_UNLOCKED);
    CHECK_EQ(io.lock, 1);
    CHECK_EQ(io.timer_ms, 5000);
    CHECK_EQ(io.sound, BUZZ_OK);
    CHECK_EQ(io.log_type, LOGE_ACCESS_GRANTED);
    CHECK_EQ(io.log_uid0, USER);

    send(event(EV_TIMEOUT), 6000);
    CHECK_EQ(c.state, AC_LOCKED);
    CHECK_EQ(io.lock, 0);
}

static void test_late_timer_event_is_ignored(void)
{
    start_locked();
    send(card(USER), 1000);
    send(event(EV_TIMEOUT), 1500);
    CHECK_EQ(c.state, AC_UNLOCKED);
    CHECK_EQ(io.lock, -1);
}

static void test_second_card_restarts_open_time(void)
{
    start_locked();
    send(card(USER), 1000);
    send(card(USER), 3000);
    CHECK_EQ(io.lock, -1);
    CHECK_EQ(io.timer_ms, 5000);
    send(event(EV_TIMEOUT), 6000);
    CHECK_EQ(c.state, AC_UNLOCKED);
    send(event(EV_TIMEOUT), 8000);
    CHECK_EQ(c.state, AC_LOCKED);
}

static void test_stranger_is_denied(void)
{
    start_locked();
    send(card(STRANGER), 1000);
    CHECK_EQ(c.state, AC_DENIED);
    CHECK_EQ(io.sound, BUZZ_DENIED);
    CHECK_EQ(io.log_type, LOGE_ACCESS_DENIED);
    CHECK_EQ(io.lock, -1);
    CHECK_EQ(io.timer_ms, (int)CFG_DENIED_SHOW_MS);
    send(event(EV_TIMEOUT), 1000 + CFG_DENIED_SHOW_MS);
    CHECK_EQ(c.state, AC_LOCKED);
}

static void test_five_failures_lock_the_reader(void)
{
    start_locked();
    for (uint32_t i = 1; i < CFG_LOCKOUT_ATTEMPTS; i++) {
        send(card(STRANGER), i * 1000);
        CHECK_EQ(c.state, AC_DENIED);
    }
    send(card(STRANGER), CFG_LOCKOUT_ATTEMPTS * 1000);
    CHECK_EQ(c.state, AC_LOCKOUT);
    CHECK_EQ(io.sound, BUZZ_LOCKOUT);
    CHECK_EQ(io.log_type, LOGE_LOCKOUT);
    CHECK_EQ(io.timer_ms, (int)CFG_LOCKOUT_TIME_MS);

    send(card(USER), 10000);
    CHECK_EQ(c.state, AC_LOCKOUT);
    CHECK_EQ(io.lock, -1);
    CHECK_EQ(io.log_type, LOGE_ACCESS_DENIED);

    send(event(EV_TIMEOUT), CFG_LOCKOUT_ATTEMPTS * 1000 + CFG_LOCKOUT_TIME_MS);
    CHECK_EQ(c.state, AC_LOCKED);
    send(card(STRANGER), 70000);
    CHECK_EQ(c.state, AC_DENIED);
}

static void test_old_failures_do_not_count(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS - 1U; i++) {
        send(card(STRANGER), i * 100);
    }
    send(card(STRANGER), CFG_LOCKOUT_WINDOW_MS + 1000);
    CHECK_EQ(c.state, AC_DENIED);
}

static void test_valid_card_resets_failures(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS - 1U; i++) {
        send(card(STRANGER), i * 100);
    }
    send(card(USER), 1000);
    send(event(EV_TIMEOUT), 6000);
    send(card(STRANGER), 7000);
    CHECK_EQ(c.state, AC_DENIED);
}

static void test_exit_button_opens_during_lockout(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS; i++) {
        send(card(STRANGER), i * 100);
    }
    send(event(EV_EXIT_BUTTON), 2000);
    CHECK_EQ(c.state, AC_UNLOCKED);
    CHECK_EQ(io.lock, 1);
    CHECK_EQ(io.log_type, LOGE_EXIT_BUTTON);
}

static void test_reader_fault_and_recovery(void)
{
    start_locked();
    send(event(EV_RFID_FAULT), 100);
    CHECK_EQ(c.state, AC_FAULT);
    CHECK_EQ(io.log_type, LOGE_RFID_FAULT);
    send(event(EV_RFID_FAULT), 200);
    CHECK_EQ(io.logs, 0);
    send(event(EV_RFID_OK), 300);
    CHECK_EQ(c.state, AC_LOCKED);
    CHECK_EQ(io.log_type, LOGE_RFID_RECOVERED);
}

static void test_exit_button_works_with_broken_reader(void)
{
    start_locked();
    send(event(EV_RFID_FAULT), 100);
    send(event(EV_EXIT_BUTTON), 200);
    CHECK_EQ(c.state, AC_UNLOCKED);
    send(event(EV_TIMEOUT), 5200);
    CHECK_EQ(c.state, AC_FAULT);
    CHECK_EQ(io.lock, 0);
}

static void test_fault_does_not_close_an_open_door(void)
{
    start_locked();
    send(card(USER), 0);
    send(event(EV_RFID_FAULT), 100);
    CHECK_EQ(c.state, AC_UNLOCKED);
    CHECK_EQ(io.lock, -1);
    send(event(EV_TIMEOUT), 5000);
    CHECK_EQ(c.state, AC_FAULT);
}

static void test_admin_card_opens_menu(void)
{
    start_locked();
    send(card(ADMIN), 100);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
    CHECK_EQ(c.menu_pos, 0);
    CHECK_EQ(io.timer_ms, (int)CFG_MENU_IDLE_MS);
    CHECK_EQ(io.log_type, LOGE_ADMIN_LOGIN);
    CHECK_EQ(io.lock, -1);
}

static void test_menu_cursor_wraps_and_back_closes(void)
{
    start_locked();
    open_menu(0);
    send(joy(JOY_UP), 10);
    CHECK_EQ(c.menu_pos, MENU_COUNT - 1);
    CHECK_EQ(io.sound, BUZZ_CLICK);
    CHECK_EQ(io.screens, 1);
    send(joy(JOY_DOWN), 20);
    CHECK_EQ(c.menu_pos, 0);
    send(joy(JOY_LEFT), 30);
    CHECK_EQ(c.state, AC_LOCKED);
}

static void test_menu_closes_after_idle_time(void)
{
    start_locked();
    open_menu(0);
    send(joy(JOY_DOWN), 20000);
    send(event(EV_TIMEOUT), CFG_MENU_IDLE_MS);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
    send(event(EV_TIMEOUT), 20000 + CFG_MENU_IDLE_MS);
    CHECK_EQ(c.state, AC_LOCKED);
}

static void test_menu_open_door(void)
{
    start_locked();
    open_menu(0);
    send(event(EV_JOY_CLICK), 10);
    CHECK_EQ(c.state, AC_UNLOCKED);
    CHECK_EQ(io.lock, 1);
    CHECK_EQ(io.sound, BUZZ_OK);
}

static void test_add_user_card(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_ADD_CARD, 10);
    CHECK_EQ(c.state, AC_ENROLL_WAIT_CARD);
    CHECK(!c.adding_admin);

    send(card(STRANGER), 20);
    CHECK_EQ(io.saves, 1);
    CHECK_EQ(cards.count, 3);
    CHECK_EQ(class_of(STRANGER), CARD_USER);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_OK);
    CHECK_EQ(c.result, RES_CARD_ADDED);
    CHECK_EQ(io.log_type, LOGE_CARD_ADDED);

    send(event(EV_TIMEOUT), 20 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
}

static void test_add_admin_card(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_ADD_ADMIN, 10);
    CHECK(c.adding_admin);
    send(card(STRANGER), 20);
    CHECK_EQ(class_of(STRANGER), CARD_ADMIN);
    CHECK_EQ(c.result, RES_ADMIN_ADDED);
    send(event(EV_TIMEOUT), 20 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
}

static void test_add_card_after_add_admin_adds_user(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_ADD_ADMIN, 10);
    send(joy(JOY_LEFT), 20);
    choose(MENU_ADD_CARD, 30);
    send(card(STRANGER), 40);
    CHECK_EQ(class_of(STRANGER), CARD_USER);
}

static void test_known_card_is_not_added_twice(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_ADD_CARD, 10);
    send(card(USER), 20);
    CHECK_EQ(io.saves, 0);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_FAIL);
    CHECK_EQ(c.result, RES_ALREADY_EXISTS);
}

static void test_random_uid_is_not_added(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_ADD_CARD, 10);
    send(card(RANDOM), 20);
    CHECK_EQ(io.saves, 0);
    CHECK_EQ(cards.count, 2);
    CHECK_EQ(c.result, RES_RANDOM_UID);
    send(event(EV_TIMEOUT), 20 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
}

static void test_flash_error_undoes_the_change(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_ADD_CARD, 10);
    io.save_ok = false;
    send(card(STRANGER), 20);
    CHECK_EQ(cards.count, 2);
    CHECK_EQ(class_of(STRANGER), CARD_UNKNOWN);
    CHECK_EQ(c.result, RES_STORAGE_ERROR);
    CHECK_EQ(io.sound, BUZZ_ERROR);
    CHECK_EQ(io.logs, 0);
}

static void test_waiting_for_card_times_out_to_menu(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_ADD_CARD, 10);
    send(event(EV_TIMEOUT), 10 + CFG_ENROLL_WAIT_MS);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
}

static void test_delete_card(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_DELETE_CARD, 10);
    CHECK_EQ(c.state, AC_DELETE_SELECT);
    send(joy(JOY_DOWN), 20);
    CHECK_EQ(c.list_pos, 1);
    send(joy(JOY_DOWN), 30);
    CHECK_EQ(c.list_pos, 0);
    send(joy(JOY_DOWN), 40);

    send(event(EV_JOY_CLICK), 50);
    CHECK_EQ(cards.count, 1);
    CHECK_EQ(class_of(USER), CARD_UNKNOWN);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_OK);
    CHECK_EQ(io.log_type, LOGE_CARD_REMOVED);
    CHECK_EQ(io.log_uid0, USER);
    CHECK_EQ(c.list_pos, 0);
}

static void test_last_admin_is_not_deleted(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_DELETE_CARD, 10);
    send(event(EV_JOY_CLICK), 20);
    CHECK_EQ(cards.count, 2);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_FAIL);
    CHECK_EQ(c.result, RES_LAST_ADMIN);
}

static void test_change_open_time(void)
{
    start_locked();
    open_menu(0);
    choose(MENU_UNLOCK_TIME, 10);
    CHECK_EQ(c.state, AC_SETTINGS);
    send(joy(JOY_UP), 20);
    send(joy(JOY_UP), 30);
    CHECK_EQ(c.edit_seconds, 7);
    CHECK_EQ(cards.unlock_time_s, 5);

    send(event(EV_JOY_CLICK), 40);
    CHECK_EQ(io.saves, 1);
    CHECK_EQ(cards.unlock_time_s, 7);
    CHECK_EQ(io.log_type, LOGE_SETTINGS_CHANGED);

    send(event(EV_JOY_CLICK), 50);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
    send(joy(JOY_LEFT), 60);
    send(card(USER), 100);
    CHECK_EQ(io.timer_ms, 7000);
}

static void test_open_time_limits(void)
{
    start_locked();
    cards.unlock_time_s = CFG_UNLOCK_TIME_MIN_S;
    open_menu(0);
    choose(MENU_UNLOCK_TIME, 10);
    send(joy(JOY_DOWN), 20);
    CHECK_EQ(c.edit_seconds, CFG_UNLOCK_TIME_MIN_S);
    for (int i = 0; i < 100; i++) {
        send(joy(JOY_UP), 30);
    }
    CHECK_EQ(c.edit_seconds, CFG_UNLOCK_TIME_MAX_S);
    send(joy(JOY_LEFT), 40);
    CHECK_EQ(cards.unlock_time_s, CFG_UNLOCK_TIME_MIN_S);
}

static void test_setup_ignores_cards_until_exit_button(void)
{
    start(false);
    send(card(STRANGER), 100);
    CHECK_EQ(io.saves, 0);
    CHECK_EQ(io.sound, BUZZ_DENIED);
    CHECK_EQ(c.state, AC_SETUP);
}

static void test_setup_registers_first_admin(void)
{
    start_setup_armed();
    CHECK_EQ(c.state, AC_SETUP);
    send(card(STRANGER), 6000);
    CHECK_EQ(class_of(STRANGER), CARD_ADMIN);
    CHECK_EQ(c.result, RES_ADMIN_ADDED);
    CHECK(!AC_SetupArmed(&c, 6001));
    send(event(EV_TIMEOUT), 6000 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(c.state, AC_LOCKED);
}

static void test_setup_arming_expires(void)
{
    start_setup_armed();
    CHECK(AC_SetupArmed(&c, 6000));
    CHECK(!AC_SetupArmed(&c, 10 + CFG_SETUP_ARM_MS));
    send(card(STRANGER), 10 + CFG_SETUP_ARM_MS + 1U);
    CHECK_EQ(io.saves, 0);
}

static void test_setup_rejects_random_uid_but_stays_armed(void)
{
    start_setup_armed();
    send(card(RANDOM), 6000);
    CHECK_EQ(c.result, RES_RANDOM_UID);
    send(event(EV_TIMEOUT), 6000 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(c.state, AC_SETUP);
    send(card(STRANGER), 8000);
    CHECK_EQ(c.result, RES_ADMIN_ADDED);
}

static void test_setup_flash_error_stays_in_setup(void)
{
    start_setup_armed();
    io.save_ok = false;
    send(card(STRANGER), 6000);
    CHECK_EQ(cards.count, 0);
    send(event(EV_TIMEOUT), 6000 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(c.state, AC_SETUP);
}

static void test_exit_button_arms_setup_only_without_admin(void)
{
    start_locked();
    send(event(EV_EXIT_BUTTON), 10);
    CHECK(!AC_SetupArmed(&c, 20));
}

static void test_phone_unlock(void)
{
    start_locked();
    send(event(EV_NET_UNLOCK), 100);
    CHECK_EQ(c.state, AC_UNLOCKED);
    CHECK_EQ(io.log_type, LOGE_NET_UNLOCK);
    send(event(EV_TIMEOUT), 5100);

    open_menu(6000);
    send(event(EV_NET_UNLOCK), 6100);
    CHECK_EQ(c.state, AC_ADMIN_MENU);
}

static void test_lockout_countdown(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS; i++) {
        send(card(STRANGER), 0);
    }
    CHECK_EQ(AC_SecondsLeft(&c, 0), CFG_LOCKOUT_TIME_MS / 1000U);
    CHECK_EQ(AC_SecondsLeft(&c, CFG_LOCKOUT_TIME_MS - 1500U), 2);
    CHECK_EQ(AC_SecondsLeft(&c, CFG_LOCKOUT_TIME_MS + 10U), 0);
}

int main(void)
{
    printf("test_access_ctrl\n");
    RUN(test_start_closes_the_lock);
    RUN(test_start_without_admin_is_setup);
    RUN(test_user_card_opens_then_closes);
    RUN(test_late_timer_event_is_ignored);
    RUN(test_second_card_restarts_open_time);
    RUN(test_stranger_is_denied);
    RUN(test_five_failures_lock_the_reader);
    RUN(test_old_failures_do_not_count);
    RUN(test_valid_card_resets_failures);
    RUN(test_exit_button_opens_during_lockout);
    RUN(test_reader_fault_and_recovery);
    RUN(test_exit_button_works_with_broken_reader);
    RUN(test_fault_does_not_close_an_open_door);
    RUN(test_admin_card_opens_menu);
    RUN(test_menu_cursor_wraps_and_back_closes);
    RUN(test_menu_closes_after_idle_time);
    RUN(test_menu_open_door);
    RUN(test_add_user_card);
    RUN(test_add_admin_card);
    RUN(test_add_card_after_add_admin_adds_user);
    RUN(test_known_card_is_not_added_twice);
    RUN(test_random_uid_is_not_added);
    RUN(test_flash_error_undoes_the_change);
    RUN(test_waiting_for_card_times_out_to_menu);
    RUN(test_delete_card);
    RUN(test_last_admin_is_not_deleted);
    RUN(test_change_open_time);
    RUN(test_open_time_limits);
    RUN(test_setup_ignores_cards_until_exit_button);
    RUN(test_setup_registers_first_admin);
    RUN(test_setup_arming_expires);
    RUN(test_setup_rejects_random_uid_but_stays_armed);
    RUN(test_setup_flash_error_stays_in_setup);
    RUN(test_exit_button_arms_setup_only_without_admin);
    RUN(test_phone_unlock);
    RUN(test_lockout_countdown);
    return TEST_REPORT();
}
