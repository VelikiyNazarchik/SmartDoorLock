#include "main.h"
#include "app_config.h"
#include "app_time.h"
#include "timeutil.h"

/* Налаштування RTC регістрами: RM0383, розділи 6.3.20 (RCC_BDCR) і 17 (RTC). */

#define RTC_SYNC_TIMEOUT_MS  50U
#define RTC_BKP_COUNT        20U

static volatile bool s_ready;
static TimeSource    s_source;

static void backup_write_enable(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;
}

static void rtc_unlock(void)
{
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
}

static void rtc_lock(void)
{
    RTC->WPR = 0xFFU;
}

static bool wait_flag(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t t0 = HAL_GetTick();
    while ((*reg & mask) == 0U) {
        if ((HAL_GetTick() - t0) > RTC_SYNC_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

static bool enter_init(void)
{
    RTC->ISR |= RTC_ISR_INIT;
    return wait_flag(&RTC->ISR, RTC_ISR_INITF);
}

static bool wait_sync(void)
{
    RTC->ISR &= ~RTC_ISR_RSF;
    return wait_flag(&RTC->ISR, RTC_ISR_RSF);
}

static uint8_t bcd2(uint32_t v)
{
    return (uint8_t)(((v >> 4) & 0x0FU) * 10U + (v & 0x0FU));
}

static uint32_t to_bcd(uint32_t v)
{
    return ((v / 10U) << 4) | (v % 10U);
}

TimeSource TIME_Init(void (*wait_ms)(uint32_t ms), void (*kick)(void))
{
    backup_write_enable();

    if ((RCC->BDCR & RCC_BDCR_RTCEN) != 0U) {
        uint32_t sel = RCC->BDCR & RCC_BDCR_RTCSEL;
        s_source = (sel == RCC_BDCR_RTCSEL_0) ? TIME_SRC_LSE :
                   (sel == RCC_BDCR_RTCSEL_1) ? TIME_SRC_LSI : TIME_SRC_NONE;
        rtc_unlock();
        (void)wait_sync();
        rtc_lock();
        s_ready = (s_source != TIME_SRC_NONE);
        return s_source;
    }

    if ((RCC->BDCR & RCC_BDCR_RTCSEL) != 0U) {
        /* джерело тактування RTC змінюється лише через скидання backup-домену */
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;
    }

    RCC->BDCR |= RCC_BDCR_LSEON;
    uint32_t waited = 0;
    while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U && waited < CFG_LSE_START_TIMEOUT_MS) {
        if (kick != NULL) {
            kick();
        }
        if (wait_ms != NULL) {
            wait_ms(10);
        } else {
            HAL_Delay(10);
        }
        waited += 10U;
    }

    uint32_t prediv_s;
    if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) {
        RCC->BDCR |= RCC_BDCR_RTCSEL_0;
        s_source = TIME_SRC_LSE;
        prediv_s = 255U;
    } else {
        RCC->BDCR &= ~RCC_BDCR_LSEON;
        RCC->CSR |= RCC_CSR_LSION;
        (void)wait_flag(&RCC->CSR, RCC_CSR_LSIRDY);
        RCC->BDCR |= RCC_BDCR_RTCSEL_1;
        s_source = TIME_SRC_LSI;
        prediv_s = 249U;
    }
    RCC->BDCR |= RCC_BDCR_RTCEN;

    rtc_unlock();
    bool ok = enter_init();
    if (ok) {
        RTC->PRER = prediv_s;                        /* PREDIV_S записується першим */
        RTC->PRER |= (127U << RTC_PRER_PREDIV_A_Pos);
        RTC->CR &= ~RTC_CR_FMT;
        RTC->ISR &= ~RTC_ISR_INIT;
        ok = wait_sync();
    }
    rtc_lock();
    s_ready = ok;
    if (!ok) {
        s_source = TIME_SRC_NONE;
    }
    return s_source;
}

TimeSource TIME_Source(void)
{
    return s_source;
}

const char *TIME_SourceName(void)
{
    switch (s_source) {
    case TIME_SRC_LSE: return "LSE";
    case TIME_SRC_LSI: return "LSI";
    default:           return "none";
    }
}

bool TIME_IsSet(void)
{
    return s_ready && (RTC->ISR & RTC_ISR_INITS) != 0U;
}

uint32_t TIME_Now(void)
{
    if (!TIME_IsSet()) {
        return 0;
    }
    uint32_t tr = RTC->TR;   /* читання TR фіксує DR, доки його не прочитають */
    uint32_t dr = RTC->DR;
    TU_DateTime dt = {
        .year = (uint16_t)(2000U + bcd2((dr >> 16) & 0xFFU)),
        .month = bcd2((dr >> 8) & 0x1FU),
        .day = bcd2(dr & 0x3FU),
        .hour = bcd2((tr >> 16) & 0x3FU),
        .minute = bcd2((tr >> 8) & 0x7FU),
        .second = bcd2(tr & 0x7FU),
        .weekday = (uint8_t)((dr >> 13) & 0x07U),
    };
    return TU_IsValid(&dt) ? TU_ToEpoch(&dt) : 0U;
}

bool TIME_Set(uint32_t epoch)
{
    if (!s_ready) {
        return false;
    }
    TU_DateTime dt;
    TU_FromEpoch(epoch, &dt);
    uint32_t tr = (to_bcd(dt.hour) << 16) | (to_bcd(dt.minute) << 8) | to_bcd(dt.second);
    uint32_t dr = (to_bcd(dt.year - 2000U) << 16) | ((uint32_t)dt.weekday << 13) |
                  (to_bcd(dt.month) << 8) | to_bcd(dt.day);

    backup_write_enable();
    rtc_unlock();
    bool ok = enter_init();
    if (ok) {
        RTC->TR = tr;
        RTC->DR = dr;
        RTC->ISR &= ~RTC_ISR_INIT;
        ok = wait_sync();
    }
    rtc_lock();
    return ok;
}

uint32_t TIME_BkpRead(uint32_t idx)
{
    if (idx >= RTC_BKP_COUNT) {
        return 0;
    }
    return (&RTC->BKP0R)[idx];
}

void TIME_BkpWrite(uint32_t idx, uint32_t value)
{
    if (idx >= RTC_BKP_COUNT) {
        return;
    }
    backup_write_enable();
    (&RTC->BKP0R)[idx] = value;
}
