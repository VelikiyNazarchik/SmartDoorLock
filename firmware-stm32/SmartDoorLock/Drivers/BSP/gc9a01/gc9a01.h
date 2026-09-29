#ifndef GC9A01_H
#define GC9A01_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define LCD_WIDTH   240U
#define LCD_HEIGHT  240U

typedef enum {
    LCD_OK = 0,
    LCD_ERR_PARAM,
    LCD_ERR_SPI,
    LCD_ERR_DMA_TIMEOUT
} LCD_Status;

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    GPIO_TypeDef      *dc_port;
    uint16_t           dc_pin;
    GPIO_TypeDef      *rst_port;
    uint16_t           rst_pin;
    /* Blocks until LCD_OnDmaComplete() runs; returns false on timeout.
     * Under an RTOS this waits on a thread flag so the CPU is free during the transfer.
     * NULL = spin on the busy flag. */
    bool             (*dma_wait)(void);
    bool               use_dma;
    volatile bool      dma_busy;
} LCD_Handle;

LCD_Status LCD_Init(LCD_Handle *h);
LCD_Status LCD_SetWindow(LCD_Handle *h, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
/* Streams pixels into the current window. Data must already be big-endian RGB565
 * (high byte first on the wire). Uses DMA for large blocks when h->use_dma is set. */
LCD_Status LCD_WritePixels(LCD_Handle *h, const uint16_t *px_be, uint32_t count);
void       LCD_OnDmaComplete(LCD_Handle *h);

static inline uint16_t LCD_Swap16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

#endif
