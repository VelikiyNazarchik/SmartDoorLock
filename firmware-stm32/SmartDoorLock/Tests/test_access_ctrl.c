#include "test.h"
#include "access_ctrl.h"
#include "menu.h"

static AccessCtx c;

static AppEvent card(CardClass cls, uint8_t id)
{
    AppEvent e;
    memset(&e, 0, sizeof(e));
    e.type = EV_CARD;
    e.card.uid[0] = id;
    e.card.uid[1] = 0xA1;
    e.card.uid[2] = 0xB2;
    e.card.uid[3] = 0xC3;
    e.card.len = 4;
    e.card.cls = cls;
    return e;
}

static AppEvent ev(AppEventType t)
{
    AppEvent e;
    memset(&e, 0, sizeof(e));
    e.type = t;
    return e;
}

static AppEvent joy(JOY_Direction d)
{
    AppEvent e = ev(EV_JOY_DIR);
    e.dir = d;
    return e;
}

static AppEvent result(bool ok, UiResult reason)
{
    AppEvent e = ev(EV_CMD_RESULT);
    e.result.ok = ok;
    e.result.reason = (uint8_t)reason;
    e.result.uid[0] = 0x42;
    e.result.uid_len = 4;
    return e;
}

static AccessActions send(AppEvent e, uint32_t now)
{
    return AC_HandleEvent(&c, &e, now);
}

/* Locked door, 2 cards (one admin), 5 s unlock time. */
static void start_locked(void)
{
    AC_Init(&c, 5);
    AC_SetCounts(&c, 2, 1, 0);
    (void)AC_Start(&c, 0);
}

static void start_setup_armed(void);

static void open_menu(uint32_t now)
{
    (void)send(card(CARD_ADMIN, 1), now);
}

static void menu_select(MenuItem item, uint32_t now)
{
    while (c.menu_sel != (uint8_t)item) {
        (void)send(joy(JOY_DOWN), now);
    }
    (void)send(ev(EV_JOY_CLICK), now);
}

/* ------------------------------------------------------------------------------------------ */

static void test_start_locked_closes_lock(void)
{
    AC_Init(&c, 5);
    AC_SetCounts(&c, 2, 1, 0);
    AccessActions a = AC_Start(&c, 0);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
    CHECK_EQ(a.lock, LOCK_ACT_CLOSE);
    CHECK(a.redraw);
    CHECK_EQ(AC_Screen(&c), SCR_LOCKED);
}

static void test_start_setup_without_admin(void)
{
    AC_Init(&c, 5);
    AC_SetCounts(&c, 0, 0, 0);
    (void)AC_Start(&c, 0);
    CHECK_EQ(AC_GetState(&c), AC_SETUP);
    CHECK_EQ(AC_Screen(&c), SCR_SETUP);
}

static void test_user_card_unlocks_then_relocks(void)
{
    start_locked();
    AccessActions a = send(card(CARD_USER, 7), 1000);
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    CHECK_EQ(a.lock, LOCK_ACT_OPEN);
    CHECK_EQ(a.timer_ms, 5000);
    CHECK_EQ(a.sound, BUZZ_OK);
    CHECK(a.log);
    CHECK_EQ(a.log_type, LOGE_ACCESS_GRANTED);
    CHECK_EQ(a.uid[0], 7);
    CHECK(a.notify_net);

    a = send(ev(EV_TIMEOUT), 6000);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
    CHECK_EQ(a.lock, LOCK_ACT_CLOSE);
}

static void test_stale_timeout_is_ignored(void)
{
    start_locked();
    (void)send(card(CARD_USER, 7), 1000);
    AccessActions a = send(ev(EV_TIMEOUT), 1500);
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    CHECK_EQ(a.lock, LOCK_ACT_NONE);
    CHECK(!a.redraw);
}

static void test_second_card_extends_unlock(void)
{
    start_locked();
    (void)send(card(CARD_USER, 7), 1000);
    AccessActions a = send(card(CARD_USER, 8), 3000);
    CHECK_EQ(a.lock, LOCK_ACT_NONE);       /* already open */
    CHECK_EQ(a.timer_ms, 5000);
    (void)send(ev(EV_TIMEOUT), 6000);      /* old deadline: stale */
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    (void)send(ev(EV_TIMEOUT), 8000);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
}

static void test_unknown_card_denied(void)
{
    start_locked();
    AccessActions a = send(card(CARD_UNKNOWN, 9), 1000);
    CHECK_EQ(AC_GetState(&c), AC_DENIED);
    CHECK_EQ(a.sound, BUZZ_DENIED);
    CHECK_EQ(a.log_type, LOGE_ACCESS_DENIED);
    CHECK_EQ(a.lock, LOCK_ACT_NONE);
    CHECK_EQ(a.timer_ms, (int32_t)CFG_DENIED_SHOW_MS);
    (void)send(ev(EV_TIMEOUT), 1000 + CFG_DENIED_SHOW_MS);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
}

static void test_lockout_after_five_failures(void)
{
    start_locked();
    AccessActions a;
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS - 1U; i++) {
        a = send(card(CARD_UNKNOWN, 9), 1000 + i * 1000);
        CHECK_EQ(AC_GetState(&c), AC_DENIED);
    }
    a = send(card(CARD_UNKNOWN, 9), 1000 + CFG_LOCKOUT_ATTEMPTS * 1000);
    CHECK_EQ(AC_GetState(&c), AC_LOCKOUT);
    CHECK_EQ(a.sound, BUZZ_LOCKOUT);
    CHECK_EQ(a.log_type, LOGE_LOCKOUT);
    CHECK_EQ(a.timer_ms, (int32_t)CFG_LOCKOUT_TIME_MS);
    CHECK_EQ(AC_Screen(&c), SCR_LOCKOUT);

    /* even a valid card is refused while locked out */
    a = send(card(CARD_USER, 7), 10000);
    CHECK_EQ(AC_GetState(&c), AC_LOCKOUT);
    CHECK_EQ(a.lock, LOCK_ACT_NONE);
    CHECK_EQ(a.log_type, LOGE_ACCESS_DENIED);

    (void)send(ev(EV_TIMEOUT), 6000 + CFG_LOCKOUT_TIME_MS);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);

    /* counter was reset: one more failure is just a denial */
    (void)send(card(CARD_UNKNOWN, 9), 70000);
    CHECK_EQ(AC_GetState(&c), AC_DENIED);
}

static void test_old_failures_leave_the_window(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS - 1U; i++) {
        (void)send(card(CARD_UNKNOWN, 9), i * 100);
    }
    (void)send(card(CARD_UNKNOWN, 9), CFG_LOCKOUT_WINDOW_MS + 1000);
    CHECK_EQ(AC_GetState(&c), AC_DENIED);
}

static void test_valid_card_resets_failure_counter(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS - 1U; i++) {
        (void)send(card(CARD_UNKNOWN, 9), i * 100);
    }
    (void)send(card(CARD_USER, 7), 1000);
    (void)send(ev(EV_TIMEOUT), 6000);
    (void)send(card(CARD_UNKNOWN, 9), 7000);
    CHECK_EQ(AC_GetState(&c), AC_DENIED);
}

static void test_exit_button_works_during_lockout(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS; i++) {
        (void)send(card(CARD_UNKNOWN, 9), i * 100);
    }
    CHECK_EQ(AC_GetState(&c), AC_LOCKOUT);
    AccessActions a = send(ev(EV_EXIT_BUTTON), 2000);
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    CHECK_EQ(a.lock, LOCK_ACT_OPEN);
    CHECK_EQ(a.log_type, LOGE_EXIT_BUTTON);
}

static void test_rfid_fault_and_recovery(void)
{
    start_locked();
    AccessActions a = send(ev(EV_RFID_FAULT), 100);
    CHECK_EQ(AC_GetState(&c), AC_FAULT);
    CHECK_EQ(a.log_type, LOGE_RFID_FAULT);
    CHECK(a.notify_net);

    a = send(ev(EV_RFID_FAULT), 200);          /* repeated fault: no new log entry */
    CHECK(!a.log);

    a = send(ev(EV_RFID_OK), 300);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
    CHECK_EQ(a.log_type, LOGE_RFID_RECOVERED);
}

static void test_exit_button_works_in_fault(void)
{
    start_locked();
    (void)send(ev(EV_RFID_FAULT), 100);
    (void)send(ev(EV_EXIT_BUTTON), 200);
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    AccessActions a = send(ev(EV_TIMEOUT), 5200);
    CHECK_EQ(AC_GetState(&c), AC_FAULT);
    CHECK_EQ(a.lock, LOCK_ACT_CLOSE);
}

static void test_fault_while_unlocked_keeps_door_open(void)
{
    start_locked();
    (void)send(card(CARD_USER, 7), 0);
    AccessActions a = send(ev(EV_RFID_FAULT), 100);
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    CHECK_EQ(a.lock, LOCK_ACT_NONE);
    (void)send(ev(EV_TIMEOUT), 5000);
    CHECK_EQ(AC_GetState(&c), AC_FAULT);
}

static void test_admin_card_opens_menu(void)
{
    start_locked();
    AccessActions a = send(card(CARD_ADMIN, 1), 100);
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
    CHECK_EQ(AC_Screen(&c), SCR_MENU);
    CHECK_EQ(c.menu_sel, 0);
    CHECK_EQ(a.timer_ms, (int32_t)CFG_MENU_IDLE_MS);
    CHECK_EQ(a.log_type, LOGE_ADMIN_LOGIN);
    CHECK_EQ(a.lock, LOCK_ACT_NONE);
}

static void test_menu_navigation_wraps(void)
{
    start_locked();
    open_menu(0);
    AccessActions a = send(joy(JOY_UP), 10);
    CHECK_EQ(c.menu_sel, MENU_COUNT - 1);
    CHECK_EQ(a.sound, BUZZ_CLICK);
    CHECK(a.redraw);
    (void)send(joy(JOY_DOWN), 20);
    CHECK_EQ(c.menu_sel, 0);
    (void)send(joy(JOY_LEFT), 30);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
}

static void test_menu_idle_timeout(void)
{
    start_locked();
    open_menu(0);
    (void)send(joy(JOY_DOWN), 20000);           /* activity re-arms the idle timer */
    (void)send(ev(EV_TIMEOUT), CFG_MENU_IDLE_MS);
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
    (void)send(ev(EV_TIMEOUT), 20000 + CFG_MENU_IDLE_MS);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
}

static void test_menu_open_door(void)
{
    start_locked();
    open_menu(0);
    AccessActions a = send(ev(EV_JOY_CLICK), 10);
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    CHECK_EQ(a.lock, LOCK_ACT_OPEN);
}

static void test_enroll_new_card(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_ADD_CARD, 10);
    CHECK_EQ(AC_GetState(&c), AC_ENROLL_WAIT_CARD);

    AccessActions a = send(card(CARD_UNKNOWN, 0x55), 20);
    CHECK_EQ(a.cmd, AC_CMD_ENROLL_USER);
    CHECK_EQ(a.uid[0], 0x55);
    CHECK_EQ(a.uid_len, 4);

    AC_SetCounts(&c, 3, 1, 0);
    a = send(result(true, RES_NONE), 30);
    CHECK_EQ(AC_GetState(&c), AC_RESULT);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_OK);
    CHECK_EQ(c.result, RES_CARD_ADDED);
    CHECK_EQ(a.log_type, LOGE_CARD_ADDED);
    CHECK_EQ(a.sound, BUZZ_OK);

    (void)send(ev(EV_TIMEOUT), 30 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
}

static void test_enroll_admin_from_menu(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_ADD_ADMIN, 10);
    CHECK_EQ(AC_GetState(&c), AC_ENROLL_WAIT_CARD);
    CHECK(c.enroll_admin);

    AccessActions a = send(card(CARD_UNKNOWN, 0x66), 20);
    CHECK_EQ(a.cmd, AC_CMD_ENROLL_ADMIN);
    CHECK_EQ(a.uid[0], 0x66);

    AC_SetCounts(&c, 3, 2, 0);
    a = send(result(true, RES_NONE), 30);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_OK);
    CHECK_EQ(c.result, RES_ADMIN_ADDED);
    CHECK_EQ(a.log_type, LOGE_CARD_ADDED);

    (void)send(ev(EV_TIMEOUT), 30 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);   /* back to the menu, not to setup/locked */
}

static void test_add_card_after_add_admin_is_user(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_ADD_ADMIN, 10);
    (void)send(joy(JOY_LEFT), 20);
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
    menu_select(MENU_ADD_CARD, 30);
    CHECK(!c.enroll_admin);
    AccessActions a = send(card(CARD_UNKNOWN, 0x67), 40);
    CHECK_EQ(a.cmd, AC_CMD_ENROLL_USER);
}

static void test_random_uid_rejected_in_menu(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_ADD_CARD, 10);
    AccessActions a = send(card(CARD_UNKNOWN, 0x08), 20);   /* 08 xx xx xx = random ID */
    CHECK_EQ(a.cmd, AC_CMD_NONE);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_FAIL);
    CHECK_EQ(c.result, RES_RANDOM_UID);
    (void)send(ev(EV_TIMEOUT), 20 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
}

static void test_random_uid_rejected_in_setup_keeps_arming(void)
{
    start_setup_armed();
    AccessActions a = send(card(CARD_UNKNOWN, 0x08), 6000);
    CHECK_EQ(a.cmd, AC_CMD_NONE);
    CHECK_EQ(c.result, RES_RANDOM_UID);
    (void)send(ev(EV_TIMEOUT), 6000 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(AC_GetState(&c), AC_SETUP);
    CHECK(AC_SetupArmed(&c, 8000));
    a = send(card(CARD_UNKNOWN, 0x11), 8000);                 /* a proper card still works */
    CHECK_EQ(a.cmd, AC_CMD_ENROLL_ADMIN);
}

static void test_enroll_known_card_rejected(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_ADD_CARD, 10);
    AccessActions a = send(card(CARD_USER, 7), 20);
    CHECK_EQ(a.cmd, AC_CMD_NONE);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_FAIL);
    CHECK_EQ(c.result, RES_ALREADY_EXISTS);
}

static void test_enroll_storage_error(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_ADD_CARD, 10);
    (void)send(card(CARD_UNKNOWN, 0x55), 20);
    AccessActions a = send(result(false, RES_STORAGE_ERROR), 30);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_FAIL);
    CHECK_EQ(c.result, RES_STORAGE_ERROR);
    CHECK_EQ(a.sound, BUZZ_ERROR);
    CHECK(!a.log);
}

static void test_enroll_wait_times_out_to_menu(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_ADD_CARD, 10);
    (void)send(ev(EV_TIMEOUT), 10 + CFG_ENROLL_WAIT_MS);
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
}

static void test_delete_card(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_DELETE_CARD, 10);
    CHECK_EQ(AC_GetState(&c), AC_DELETE_SELECT);
    (void)send(joy(JOY_DOWN), 20);
    CHECK_EQ(c.list_sel, 1);
    (void)send(joy(JOY_DOWN), 30);
    CHECK_EQ(c.list_sel, 0);                  /* 2 cards: wraps */
    (void)send(joy(JOY_DOWN), 40);

    AccessActions a = send(ev(EV_JOY_CLICK), 50);
    CHECK_EQ(a.cmd, AC_CMD_DELETE);
    CHECK_EQ(a.cmd_arg, 1);

    AC_SetCounts(&c, 1, 1, 0);
    a = send(result(true, RES_NONE), 60);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_OK);
    CHECK_EQ(a.log_type, LOGE_CARD_REMOVED);
    CHECK_EQ(c.list_sel, 0);
}

static void test_delete_last_admin_refused(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_DELETE_CARD, 10);
    (void)send(ev(EV_JOY_CLICK), 20);
    (void)send(result(false, RES_LAST_ADMIN), 30);
    CHECK_EQ(AC_Screen(&c), SCR_RESULT_FAIL);
    CHECK_EQ(c.result, RES_LAST_ADMIN);
}

static void test_settings_change_unlock_time(void)
{
    start_locked();
    open_menu(0);
    menu_select(MENU_UNLOCK_TIME, 10);
    CHECK_EQ(AC_GetState(&c), AC_SETTINGS);
    (void)send(joy(JOY_UP), 20);
    (void)send(joy(JOY_UP), 30);
    CHECK_EQ(c.edit_unlock_s, 7);
    CHECK_EQ(c.unlock_time_s, 5);             /* not applied before saving */

    AccessActions a = send(ev(EV_JOY_CLICK), 40);
    CHECK_EQ(a.cmd, AC_CMD_SAVE_SETTINGS);
    CHECK_EQ(a.cmd_arg, 7);
    a = send(result(true, RES_NONE), 50);
    CHECK_EQ(c.unlock_time_s, 7);
    CHECK_EQ(a.log_type, LOGE_SETTINGS_CHANGED);

    (void)send(joy(JOY_LEFT), 60);            /* result screen ignores LEFT */
    (void)send(ev(EV_JOY_CLICK), 70);         /* click skips the result screen */
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
    (void)send(joy(JOY_LEFT), 80);
    a = send(card(CARD_USER, 7), 100);
    CHECK_EQ(a.timer_ms, 7000);
}

static void test_settings_limits(void)
{
    AC_Init(&c, CFG_UNLOCK_TIME_MIN_S);
    AC_SetCounts(&c, 2, 1, 0);
    (void)AC_Start(&c, 0);
    open_menu(0);
    menu_select(MENU_UNLOCK_TIME, 10);
    (void)send(joy(JOY_DOWN), 20);
    CHECK_EQ(c.edit_unlock_s, CFG_UNLOCK_TIME_MIN_S);
    for (int i = 0; i < 100; i++) {
        (void)send(joy(JOY_UP), 30);
    }
    CHECK_EQ(c.edit_unlock_s, CFG_UNLOCK_TIME_MAX_S);
    (void)send(joy(JOY_LEFT), 40);            /* leave without saving */
    CHECK_EQ(c.unlock_time_s, CFG_UNLOCK_TIME_MIN_S);
}

static void start_setup_armed(void)
{
    AC_Init(&c, 5);
    AC_SetCounts(&c, 0, 0, 0);
    (void)AC_Start(&c, 0);
    (void)send(ev(EV_EXIT_BUTTON), 10);       /* someone inside arms the setup */
    (void)send(ev(EV_TIMEOUT), 5010);
    CHECK_EQ(AC_GetState(&c), AC_SETUP);
}

static void test_setup_ignores_cards_until_armed(void)
{
    AC_Init(&c, 5);
    AC_SetCounts(&c, 0, 0, 0);
    (void)AC_Start(&c, 0);
    CHECK(!AC_SetupArmed(&c, 0));
    AccessActions a = send(card(CARD_UNKNOWN, 0x11), 100);
    CHECK_EQ(a.cmd, AC_CMD_NONE);
    CHECK_EQ(a.sound, BUZZ_DENIED);
    CHECK_EQ(a.log_type, LOGE_ACCESS_DENIED);
    CHECK_EQ(AC_GetState(&c), AC_SETUP);
}

static void test_setup_arming_expires(void)
{
    start_setup_armed();
    CHECK(AC_SetupArmed(&c, 6000));
    CHECK(!AC_SetupArmed(&c, 10 + CFG_SETUP_ARM_MS));
    AccessActions a = send(card(CARD_UNKNOWN, 0x11), 10 + CFG_SETUP_ARM_MS + 1U);
    CHECK_EQ(a.cmd, AC_CMD_NONE);
}

static void test_exit_button_does_not_arm_when_admin_exists(void)
{
    start_locked();
    (void)send(ev(EV_EXIT_BUTTON), 10);
    CHECK(!AC_SetupArmed(&c, 20));
}

static void test_setup_enrolls_first_admin(void)
{
    start_setup_armed();
    AccessActions a = send(card(CARD_UNKNOWN, 0x11), 6000);
    CHECK_EQ(a.cmd, AC_CMD_ENROLL_ADMIN);
    CHECK(!AC_SetupArmed(&c, 6001));          /* one card per arming */
    AC_SetCounts(&c, 1, 1, 0);
    a = send(result(true, RES_NONE), 6010);
    CHECK_EQ(c.result, RES_ADMIN_ADDED);
    CHECK_EQ(a.log_type, LOGE_CARD_ADDED);
    (void)send(ev(EV_TIMEOUT), 6010 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(AC_GetState(&c), AC_LOCKED);
}

static void test_setup_failed_enroll_stays_in_setup(void)
{
    start_setup_armed();
    (void)send(card(CARD_UNKNOWN, 0x11), 6000);
    (void)send(result(false, RES_STORAGE_ERROR), 6010);
    (void)send(ev(EV_TIMEOUT), 6010 + CFG_RESULT_SHOW_MS);
    CHECK_EQ(AC_GetState(&c), AC_SETUP);
}

static void test_net_unlock(void)
{
    start_locked();
    AccessActions a = send(ev(EV_NET_UNLOCK), 100);
    CHECK_EQ(AC_GetState(&c), AC_UNLOCKED);
    CHECK_EQ(a.log_type, LOGE_NET_UNLOCK);
    (void)send(ev(EV_TIMEOUT), 5100);

    open_menu(6000);
    a = send(ev(EV_NET_UNLOCK), 6100);        /* an admin is at the device: ignored */
    CHECK_EQ(AC_GetState(&c), AC_ADMIN_MENU);
    CHECK_EQ(a.lock, LOCK_ACT_NONE);
}

static void test_remaining_seconds(void)
{
    start_locked();
    for (uint32_t i = 0; i < CFG_LOCKOUT_ATTEMPTS; i++) {
        (void)send(card(CARD_UNKNOWN, 9), 0);
    }
    CHECK_EQ(AC_RemainingS(&c, 0), CFG_LOCKOUT_TIME_MS / 1000U);
    CHECK_EQ(AC_RemainingS(&c, CFG_LOCKOUT_TIME_MS - 1500U), 2);
    CHECK_EQ(AC_RemainingS(&c, CFG_LOCKOUT_TIME_MS + 10U), 0);
}

int main(void)
{
    printf("test_access_ctrl\n");
    RUN(test_start_locked_closes_lock);
    RUN(test_start_setup_without_admin);
    RUN(test_user_card_unlocks_then_relocks);
    RUN(test_stale_timeout_is_ignored);
    RUN(test_second_card_extends_unlock);
    RUN(test_unknown_card_denied);
    RUN(test_lockout_after_five_failures);
    RUN(test_old_failures_leave_the_window);
    RUN(test_valid_card_resets_failure_counter);
    RUN(test_exit_button_works_during_lockout);
    RUN(test_rfid_fault_and_recovery);
    RUN(test_exit_button_works_in_fault);
    RUN(test_fault_while_unlocked_keeps_door_open);
    RUN(test_admin_card_opens_menu);
    RUN(test_menu_navigation_wraps);
    RUN(test_menu_idle_timeout);
    RUN(test_menu_open_door);
    RUN(test_enroll_new_card);
    RUN(test_enroll_admin_from_menu);
    RUN(test_add_card_after_add_admin_is_user);
    RUN(test_random_uid_rejected_in_menu);
    RUN(test_random_uid_rejected_in_setup_keeps_arming);
    RUN(test_enroll_known_card_rejected);
    RUN(test_enroll_storage_error);
    RUN(test_enroll_wait_times_out_to_menu);
    RUN(test_delete_card);
    RUN(test_delete_last_admin_refused);
    RUN(test_settings_change_unlock_time);
    RUN(test_settings_limits);
    RUN(test_setup_ignores_cards_until_armed);
    RUN(test_setup_arming_expires);
    RUN(test_exit_button_does_not_arm_when_admin_exists);
    RUN(test_setup_enrolls_first_admin);
    RUN(test_setup_failed_enroll_stays_in_setup);
    RUN(test_net_unlock);
    RUN(test_remaining_seconds);
    return TEST_REPORT();
}
