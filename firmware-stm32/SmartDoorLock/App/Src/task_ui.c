#include <string.h>
#include "main.h"
#include "spi.h"
#include "gc9a01.h"
#include "gfx.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"
#include "ui.h"

static LCD_Handle s_lcd;

static bool lcd_dma_wait(void)
{
    uint32_t f = osThreadFlagsWait(FLAG_UI_DMA_DONE, osFlagsWaitAny, CFG_LCD_DMA_TIMEOUT_MS);
    return (f & osFlagsError) == 0U;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi2) {
        LCD_OnDmaComplete(&s_lcd);
        (void)osThreadFlagsSet(tUiHandle, FLAG_UI_DMA_DONE);
    }
}

static void dwt_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t measure_fill_us(bool use_dma)
{
    s_lcd.use_dma = use_dma;
    uint32_t c0 = DWT->CYCCNT;
    (void)GFX_FillScreen(GFX_BLACK);
    uint32_t cycles = DWT->CYCCNT - c0;
    return cycles / (SystemCoreClock / 1000000U);
}

/* Повний кадр = 240*240*2 = 115200 байтів; по SPI на 24 МГц це мінімум 38,4 мс.
 * За часом DMA і звичайна передача майже однакові. Різниця в іншому: з DMA процесор
 * у цей час вільний і виконує інші задачі, а без DMA він крутиться в циклі очікування. */
static void benchmark(void)
{
    dwt_start();
    uint32_t blocking_us = measure_fill_us(false);
    uint32_t dma_us = measure_fill_us(true);
    if (!s_lcd.use_dma) {
        LOG_ERR("LCD DMA timed out during the benchmark: using blocking SPI");
        return;
    }
    s_lcd.use_dma = CFG_LCD_USE_DMA;
    LOG_INFO("LCD full frame: DMA %lu us, blocking SPI %lu us (wire limit 38400 us)",
             (unsigned long)dma_us, (unsigned long)blocking_us);
}

void TaskUi(void *arg)
{
    (void)arg;
    s_lcd.hspi = &hspi2;
    s_lcd.cs_port = LCD_CS_GPIO_Port;
    s_lcd.cs_pin = LCD_CS_Pin;
    s_lcd.dc_port = LCD_DC_GPIO_Port;
    s_lcd.dc_pin = LCD_DC_Pin;
    s_lcd.rst_port = LCD_RES_GPIO_Port;
    s_lcd.rst_pin = LCD_RES_Pin;
    s_lcd.dma_wait = lcd_dma_wait;
    s_lcd.use_dma = CFG_LCD_USE_DMA;

    LCD_Status st = LCD_Init(&s_lcd);
    bool lcd_ok = (st == LCD_OK);
    if (!lcd_ok) {
        LOG_ERR("GC9A01 init failed (%d): running without display", (int)st);
    } else {
        GFX_Init(&s_lcd);
        benchmark();
    }

    UiCommand cmd;
    UiCommand shown;
    bool have_shown = false;
    memset(&cmd, 0, sizeof(cmd));
    cmd.screen = SCR_BOOT;
    if (lcd_ok) {
        UI_Draw(&cmd, NULL);
        shown = cmd;
        have_shown = true;
    }

    for (;;) {
        SUP_Heartbeat(TASK_UI);
        if (osMessageQueueGet(qUi, &cmd, NULL, 500) != osOK || !lcd_ok) {
            continue;
        }
        while (osMessageQueueGet(qUi, &cmd, NULL, 0) == osOK) {
        }
        bool dma_before = s_lcd.use_dma;
        UI_Draw(&cmd, have_shown ? &shown : NULL);
        shown = cmd;
        have_shown = true;
        if (dma_before && !s_lcd.use_dma) {
            LOG_ERR("LCD DMA timed out: switched to blocking SPI");
        }
    }
}
