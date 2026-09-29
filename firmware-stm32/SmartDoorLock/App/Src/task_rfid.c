#include <string.h>
#include "main.h"
#include "spi.h"
#include "rc522.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"

_Static_assert(RC522_UID_MAX == UID_MAX_LEN, "UID sizes must match");

static RC522_Handle s_rc;

static void rfid_yield(void)
{
    osDelay(1);
}

static void post(AppEventType type)
{
    AppEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    (void)App_PostCtrl(&ev);
}

void TaskRfid(void *arg)
{
    (void)arg;
    s_rc.hspi = &hspi1;
    s_rc.cs_port = RFID_CS_GPIO_Port;
    s_rc.cs_pin = RFID_CS_Pin;
    s_rc.rst_port = RFID_RST_GPIO_Port;
    s_rc.rst_pin = RFID_RST_Pin;
    s_rc.yield = rfid_yield;

    RC522_Status st = RC522_Init(&s_rc);
    bool fault = (st != RC522_OK);
    if (fault) {
        LOG_ERR("RC522 init failed: %s", RC522_StatusStr(st));
        post(EV_RFID_FAULT);
    } else {
        LOG_INFO("RC522 ready, version 0x%02X", s_rc.version);
    }

    uint8_t  last_uid[UID_MAX_LEN] = { 0 };
    uint8_t  last_len = 0;
    uint32_t last_seen = 0;
    uint32_t next_check = osKernelGetTickCount() + CFG_RFID_HEALTH_MS;
    uint8_t  failed_checks = 0;

    for (;;) {
        SUP_Heartbeat(TASK_RFID);
        osDelay(CFG_RFID_POLL_MS);
        uint32_t now = osKernelGetTickCount();

        if (fault) {
            if ((int32_t)(now - next_check) >= 0) {
                next_check = now + CFG_RFID_RETRY_MS;
                if (RC522_Init(&s_rc) == RC522_OK) {
                    fault = false;
                    failed_checks = 0;
                    LOG_INFO("RC522 recovered, version 0x%02X", s_rc.version);
                    post(EV_RFID_OK);
                }
            }
            continue;
        }

        if ((int32_t)(now - next_check) >= 0) {
            next_check = now + CFG_RFID_HEALTH_MS;
            st = RC522_CheckAlive(&s_rc);
            if (st != RC522_OK) {
                if (++failed_checks >= CFG_RFID_FAIL_LIMIT) {
                    fault = true;
                    next_check = now + CFG_RFID_RETRY_MS;
                    LOG_ERR("RC522 not responding (%s)", RC522_StatusStr(st));
                    post(EV_RFID_FAULT);
                    continue;
                }
            } else {
                failed_checks = 0;
            }
        }

        RC522_Uid uid;
        st = RC522_ReadUid(&s_rc, &uid);
        if (st == RC522_NO_CARD) {
            continue;
        }
        if (st != RC522_OK) {
            LOG_DEBUG("card read error: %s", RC522_StatusStr(st));
            continue;
        }

        bool same = (uid.len == last_len) && (memcmp(uid.uid, last_uid, uid.len) == 0);
        if (same && (now - last_seen) < CFG_RFID_SAME_CARD_MS) {
            continue;
        }
        memcpy(last_uid, uid.uid, uid.len);
        last_len = uid.len;
        last_seen = now;

        AppEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_CARD;
        memcpy(ev.uid, uid.uid, uid.len);
        ev.uid_len = uid.len;
        if (!App_PostCtrl(&ev)) {
            LOG_WARN("ctrl queue full, card dropped");
        }
    }
}
