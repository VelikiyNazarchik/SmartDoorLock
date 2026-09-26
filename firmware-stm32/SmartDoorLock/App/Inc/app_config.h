#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Every tunable of the application lives here: no magic numbers in the modules. */

#define APP_VERSION                "0.4.0"

/* ---- Access state machine */
#define CFG_UNLOCK_TIME_DEFAULT_S  5U       /* how long the lock stays open */
#define CFG_UNLOCK_TIME_MIN_S      1U
#define CFG_UNLOCK_TIME_MAX_S      30U
#define CFG_DENIED_SHOW_MS         1500U
#define CFG_RESULT_SHOW_MS         1500U
#define CFG_LOCKOUT_ATTEMPTS       5U       /* failed attempts ... */
#define CFG_LOCKOUT_WINDOW_MS      60000U   /* ... within this window ... */
#define CFG_LOCKOUT_TIME_MS        60000U   /* ... block the reader for this long */
#define CFG_MENU_IDLE_MS           30000U   /* admin menu closes itself after inactivity */
#define CFG_ENROLL_WAIT_MS         15000U
#define CFG_SETUP_ARM_MS           60000U   /* setup accepts an admin card this long after the exit button */
#define CFG_TIMER_SLACK_MS         20U      /* a timeout this much early is treated as stale */

/* ---- Card storage (last 128 KB Flash sector, excluded from the linker script) */
#define CFG_MAX_CARDS              64U
#define CFG_CARD_NAME_LEN          12U
#define CFG_STORAGE_ADDR           0x08060000UL
#define CFG_STORAGE_SIZE           0x20000UL
#define CFG_STORAGE_SECTOR         7U       /* FLASH_SECTOR_7 on STM32F411xE */

/* ---- Event log (RAM ring buffer) */
#define CFG_EVENT_LOG_SIZE         64U

/* ---- RFID task */
#define CFG_RFID_POLL_MS           100U
#define CFG_RFID_SAME_CARD_MS      2000U    /* ignore a repeated read of the same card */
#define CFG_RFID_HEALTH_MS         1000U    /* VersionReg check period */
#define CFG_RFID_FAIL_LIMIT        2U       /* consecutive failed checks before EV_RFID_FAULT */
#define CFG_RFID_RETRY_MS          1000U    /* re-init period while in fault */

/* ---- Input task */
#define CFG_INPUT_POLL_MS          20U
#define CFG_BTN_DEBOUNCE_MS        30U
#define CFG_JOY_REPEAT_DELAY_MS    500U     /* hold up/down: first repeat after ... */
#define CFG_JOY_REPEAT_MS          180U     /* ... then every ... */
#define CFG_JOY_DEAD_ZONE_PCT      15U
#define CFG_JOY_PRESS_PCT          60U
#define CFG_JOY_RELEASE_PCT        35U
#define CFG_JOY_INVERT_X           0
#define CFG_JOY_INVERT_Y           1        /* most modules read 0 V when pushed up */
#define CFG_JOY_SWAP_XY            0

/* ---- Lock output */
#define CFG_LOCK_ENERGIZE_TO_OPEN  1        /* 1: solenoid/strike, 0: electromagnet (fail-safe maglock) */

/* ---- Buzzer tones (TIM3 counts at 1 MHz: PSC = 95 at 96 MHz) */
#define CFG_BUZZER_TICK_HZ         1000000UL
#define CFG_BEEP_CLICK_HZ          4000U
#define CFG_BEEP_OK_HZ             2500U
#define CFG_BEEP_DENIED_HZ         600U
#define CFG_BEEP_ALARM_HZ          3500U
#define CFG_BEEP_ALARM_LOW_HZ      2200U

/* ---- Display */
#define CFG_LCD_USE_DMA            1
#define CFG_LCD_DMA_TIMEOUT_MS     100U

/* ---- Supervisor / watchdog */
#define CFG_SUPERVISOR_PERIOD_MS   250U
#define CFG_TASK_TIMEOUT_MS        2000U    /* a task silent for this long is considered hung */
#define CFG_STATS_PERIOD_MS        60000U   /* periodic stack/heap report on the log */
#define CFG_LSE_START_TIMEOUT_MS   2500U    /* RTC crystal start-up; falls back to LSI */

/* ---- Logger */
#define CFG_LOG_MSG_LEN            100U
#define CFG_LOG_QUEUE_LEN          16U
#define CFG_LOG_USB_TIMEOUT_MS     50U
#define CFG_CONSOLE_LINE_LEN       48U

/* ---- Queues */
#define CFG_QCTRL_LEN              16U
#define CFG_QUI_LEN                4U
#define CFG_QNET_LEN               8U
#define CFG_NET_LINE_LEN           64U

/* ---- Task stacks (words) */
#define CFG_STACK_CTRL             640U
#define CFG_STACK_RFID             384U
#define CFG_STACK_INPUT            384U
#define CFG_STACK_UI               768U
#define CFG_STACK_NET              256U
#define CFG_STACK_LOG              512U
#define CFG_STACK_SUPER            384U

#endif
