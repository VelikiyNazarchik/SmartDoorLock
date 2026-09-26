#include "gc9a01.h"
#include "gc9a01_init.h"

#define LCD_SPI_TIMEOUT_MS   50U
#define LCD_DMA_MIN_BYTES    64U      /* below this a blocking transfer is cheaper than DMA set-up */
#define LCD_DMA_MAX_BYTES    65534U   /* DMA NDTR is 16 bit; keep it even so no pixel is split */

static void cs(LCD_Handle *h, bool active)
{
    HAL_GPIO_WritePin(h->cs_port, h->cs_pin, active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void dc(LCD_Handle *h, bool data)
{
    HAL_GPIO_WritePin(h->dc_port, h->dc_pin, data ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static LCD_Status write_cmd(LCD_Handle *h, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    HAL_StatusTypeDef st;
    cs(h, true);
    dc(h, false);
    st = HAL_SPI_Transmit(h->hspi, &cmd, 1, LCD_SPI_TIMEOUT_MS);
    if (st == HAL_OK && len > 0U) {
        dc(h, true);
        st = HAL_SPI_Transmit(h->hspi, (uint8_t *)data, len, LCD_SPI_TIMEOUT_MS);
    }
    cs(h, false);
    return (st == HAL_OK) ? LCD_OK : LCD_ERR_SPI;
}

LCD_Status LCD_Init(LCD_Handle *h)
{
    if (h == NULL || h->hspi == NULL) {
        return LCD_ERR_PARAM;
    }
    h->dma_busy = false;
    cs(h, false);
    HAL_GPIO_WritePin(h->rst_port, h->rst_pin, GPIO_PIN_SET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(h->rst_port, h->rst_pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(h->rst_port, h->rst_pin, GPIO_PIN_SET);
    HAL_Delay(120);

    for (uint32_t i = 0; i < sizeof(gc9a01_init_seq) / sizeof(gc9a01_init_seq[0]); i++) {
        const GC9A01_InitCmd *c = &gc9a01_init_seq[i];
        LCD_Status st = write_cmd(h, c->cmd, c->data, c->len);
        if (st != LCD_OK) {
            return st;
        }
        if (c->delay_ms != 0U) {
            HAL_Delay(c->delay_ms);
        }
    }
    return LCD_OK;
}

LCD_Status LCD_SetWindow(LCD_Handle *h, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t ca[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t ra[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    LCD_Status st = write_cmd(h, GC9A01_CMD_CASET, ca, 4);
    if (st == LCD_OK) {
        st = write_cmd(h, GC9A01_CMD_RASET, ra, 4);
    }
    if (st == LCD_OK) {
        st = write_cmd(h, GC9A01_CMD_RAMWR, NULL, 0);
    }
    return st;
}

/* A DMA transfer that never completes would make every block wait for the timeout and stall the
 * owning task for seconds; after the first timeout the driver stays on blocking transfers. */
static LCD_Status dma_timed_out(LCD_Handle *h)
{
    (void)HAL_SPI_Abort(h->hspi);
    h->dma_busy = false;
    h->use_dma = false;
    return LCD_ERR_DMA_TIMEOUT;
}

static LCD_Status dma_chunk(LCD_Handle *h, const uint8_t *p, uint16_t n)
{
    h->dma_busy = true;
    if (HAL_SPI_Transmit_DMA(h->hspi, (uint8_t *)p, n) != HAL_OK) {
        h->dma_busy = false;
        return LCD_ERR_SPI;
    }
    if (h->dma_wait != NULL) {
        if (!h->dma_wait()) {
            return dma_timed_out(h);
        }
    } else {
        uint32_t t0 = HAL_GetTick();
        while (h->dma_busy) {
            if ((HAL_GetTick() - t0) > LCD_SPI_TIMEOUT_MS) {
                return dma_timed_out(h);
            }
        }
    }
    return LCD_OK;
}

LCD_Status LCD_WritePixels(LCD_Handle *h, const uint16_t *px_be, uint32_t count)
{
    const uint8_t *p = (const uint8_t *)px_be;
    uint32_t bytes = count * 2U;
    LCD_Status st = LCD_OK;

    cs(h, true);
    dc(h, true);
    while (bytes > 0U && st == LCD_OK) {
        uint32_t n = (bytes > LCD_DMA_MAX_BYTES) ? LCD_DMA_MAX_BYTES : bytes;
        if (h->use_dma && n >= LCD_DMA_MIN_BYTES) {
            st = dma_chunk(h, p, (uint16_t)n);
        } else if (HAL_SPI_Transmit(h->hspi, (uint8_t *)p, (uint16_t)n, LCD_SPI_TIMEOUT_MS) != HAL_OK) {
            st = LCD_ERR_SPI;
        }
        p += n;
        bytes -= n;
    }
    cs(h, false);
    return st;
}

void LCD_OnDmaComplete(LCD_Handle *h)
{
    h->dma_busy = false;
}
