#ifndef RC522_H
#define RC522_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#define RC522_UID_MAX 7U

typedef enum {
    RC522_OK = 0,
    RC522_NO_CARD,
    RC522_ERR_TIMEOUT,
    RC522_ERR_COLLISION,
    RC522_ERR_CRC,
    RC522_ERR_PROTOCOL,
    RC522_ERR_BCC,
    RC522_ERR_UNSUPPORTED,
    RC522_ERR_NO_CHIP,
    RC522_ERR_SPI,
    RC522_ERR_PARAM
} RC522_Status;

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    GPIO_TypeDef      *rst_port;
    uint16_t           rst_pin;
    void             (*yield)(void);
    uint8_t            version;
} RC522_Handle;

typedef struct {
    uint8_t uid[RC522_UID_MAX];
    uint8_t len;
    uint8_t sak;
} RC522_Uid;

RC522_Status RC522_Init(RC522_Handle *h);
RC522_Status RC522_CheckAlive(RC522_Handle *h);
/* REQA -> anticollision/select (cascade 1..2) -> HLTA. The card is halted afterwards, so it is reported
 * once per tap and answers again only after it leaves and re-enters the field. */
RC522_Status RC522_ReadUid(RC522_Handle *h, RC522_Uid *out);
const char  *RC522_StatusStr(RC522_Status st);

#endif
