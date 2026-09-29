#include "rc522.h"
#include "rc522_regs.h"

#define RC522_SPI_TIMEOUT_MS     10U
#define RC522_RESET_TIMEOUT_MS   50U
/* The chip timer below gives up after 25 ms; this software guard only catches a stuck chip. */
#define RC522_IRQ_GUARD_MS       40U
#define RC522_FIFO_MAX           16U

/* SPI access (datasheet 8.1.2) */

static void cs_low(RC522_Handle *h)  { HAL_GPIO_WritePin(h->cs_port, h->cs_pin, GPIO_PIN_RESET); }
static void cs_high(RC522_Handle *h) { HAL_GPIO_WritePin(h->cs_port, h->cs_pin, GPIO_PIN_SET); }

static RC522_Status reg_write(RC522_Handle *h, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7EU), val };
    cs_low(h);
    HAL_StatusTypeDef st = HAL_SPI_Transmit(h->hspi, tx, 2, RC522_SPI_TIMEOUT_MS);
    cs_high(h);
    return (st == HAL_OK) ? RC522_OK : RC522_ERR_SPI;
}

static RC522_Status reg_read(RC522_Handle *h, uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7EU) | 0x80U), 0x00U };
    uint8_t rx[2] = { 0 };
    cs_low(h);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(h->hspi, tx, rx, 2, RC522_SPI_TIMEOUT_MS);
    cs_high(h);
    *val = rx[1];
    return (st == HAL_OK) ? RC522_OK : RC522_ERR_SPI;
}

static RC522_Status reg_set_bits(RC522_Handle *h, uint8_t reg, uint8_t mask)
{
    uint8_t v;
    RC522_Status st = reg_read(h, reg, &v);
    return (st == RC522_OK) ? reg_write(h, reg, (uint8_t)(v | mask)) : st;
}

/* ISO/IEC 14443-3 CRC_A (Annex B) */

static uint16_t crc_a(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0x6363U;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        b ^= (uint8_t)(crc & 0xFFU);
        b ^= (uint8_t)(b << 4);
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ ((uint16_t)b >> 4));
    }
    return crc;
}

/* Transceive (datasheet 10.3.1.8) */

typedef struct {
    uint8_t buf[RC522_FIFO_MAX];
    uint8_t len;
    uint8_t last_bits;
} rx_frame;

static RC522_Status transceive(RC522_Handle *h, const uint8_t *tx, uint8_t tx_len, uint8_t tx_last_bits, rx_frame *rx)
{
    RC522_Status st;
    if ((st = reg_write(h, RC522_REG_COMMAND, RC522_CMD_IDLE)) != RC522_OK) return st;
    if ((st = reg_write(h, RC522_REG_COM_IRQ, 0x7FU)) != RC522_OK) return st;
    if ((st = reg_write(h, RC522_REG_FIFO_LEVEL, RC522_FIFO_FLUSH)) != RC522_OK) return st;
    for (uint8_t i = 0; i < tx_len; i++) {
        if ((st = reg_write(h, RC522_REG_FIFO_DATA, tx[i])) != RC522_OK) return st;
    }
    if ((st = reg_write(h, RC522_REG_BIT_FRAMING, (uint8_t)(tx_last_bits & 0x07U))) != RC522_OK) return st;
    if ((st = reg_write(h, RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE)) != RC522_OK) return st;
    if ((st = reg_set_bits(h, RC522_REG_BIT_FRAMING, RC522_BITFRAMING_START)) != RC522_OK) return st;

    uint32_t t0 = HAL_GetTick();
    for (;;) {
        uint8_t irq;
        if ((st = reg_read(h, RC522_REG_COM_IRQ, &irq)) != RC522_OK) return st;
        if (irq & (RC522_IRQ_RX | RC522_IRQ_IDLE)) {
            break;
        }
        if (irq & RC522_IRQ_TIMER) {
            return RC522_ERR_TIMEOUT;
        }
        if ((HAL_GetTick() - t0) > RC522_IRQ_GUARD_MS) {
            return RC522_ERR_TIMEOUT;
        }
        if (h->yield != NULL) {
            h->yield();
        }
    }

    uint8_t err;
    if ((st = reg_read(h, RC522_REG_ERROR, &err)) != RC522_OK) return st;
    if (err & (RC522_ERR_BUF_OVFL_BIT | RC522_ERR_PARITY_BIT | RC522_ERR_PROTOCOL_BIT)) {
        return RC522_ERR_PROTOCOL;
    }

    uint8_t n;
    if ((st = reg_read(h, RC522_REG_FIFO_LEVEL, &n)) != RC522_OK) return st;
    if (n > RC522_FIFO_MAX) {
        return RC522_ERR_PROTOCOL;
    }
    for (uint8_t i = 0; i < n; i++) {
        if ((st = reg_read(h, RC522_REG_FIFO_DATA, &rx->buf[i])) != RC522_OK) return st;
    }
    rx->len = n;
    uint8_t ctrl;
    if ((st = reg_read(h, RC522_REG_CONTROL, &ctrl)) != RC522_OK) return st;
    rx->last_bits = (uint8_t)(ctrl & 0x07U);

    if (err & RC522_ERR_COLL_BIT) {
        return RC522_ERR_COLLISION;
    }
    return RC522_OK;
}

RC522_Status RC522_Init(RC522_Handle *h)
{
    if (h == NULL || h->hspi == NULL || h->cs_port == NULL) {
        return RC522_ERR_PARAM;
    }
    cs_high(h);
    if (h->rst_port != NULL) {
        HAL_GPIO_WritePin(h->rst_port, h->rst_pin, GPIO_PIN_RESET);
        HAL_Delay(2);
        HAL_GPIO_WritePin(h->rst_port, h->rst_pin, GPIO_PIN_SET);
        HAL_Delay(50);   /* oscillator start-up, datasheet 8.8.2 */
    }

    RC522_Status st = reg_write(h, RC522_REG_COMMAND, RC522_CMD_SOFT_RESET);
    if (st != RC522_OK) {
        return st;
    }
    uint32_t t0 = HAL_GetTick();
    uint8_t cmd;
    do {
        HAL_Delay(1);
        if ((st = reg_read(h, RC522_REG_COMMAND, &cmd)) != RC522_OK) return st;
        if ((HAL_GetTick() - t0) > RC522_RESET_TIMEOUT_MS) {
            return RC522_ERR_NO_CHIP;
        }
    } while (cmd & RC522_COMMAND_POWER_DOWN);

    if ((st = reg_read(h, RC522_REG_VERSION, &h->version)) != RC522_OK) return st;
    if (h->version == 0x00U || h->version == 0xFFU) {
        return RC522_ERR_NO_CHIP;
    }

    static const uint8_t setup[][2] = {
        { RC522_REG_TX_MODE,     0x00 },   /* 106 kBd, no CRC: CRC_A is added in software */
        { RC522_REG_RX_MODE,     0x00 },
        { RC522_REG_MOD_WIDTH,   0x26 },
        { RC522_REG_T_MODE,      0x80 },   /* TAuto: timer starts at the end of every transmission */
        { RC522_REG_T_PRESCALER, 0xA9 },   /* 13.56 MHz / (2*169+1) = 40 kHz -> 25 us per tick */
        { RC522_REG_T_RELOAD_H,  0x03 },   /* 1000 ticks = 25 ms receive timeout */
        { RC522_REG_T_RELOAD_L,  0xE8 },
        { RC522_REG_TX_ASK,      0x40 },   /* force 100 % ASK modulation */
        { RC522_REG_MODE,        0x3D },   /* CRC preset 0x6363 (ISO 14443-3) */
        { RC522_REG_RF_CFG,      0x70 },   /* max receiver gain, 48 dB */
    };
    for (uint32_t i = 0; i < sizeof(setup) / sizeof(setup[0]); i++) {
        if ((st = reg_write(h, setup[i][0], setup[i][1])) != RC522_OK) return st;
    }
    return reg_set_bits(h, RC522_REG_TX_CONTROL, 0x03U);   /* antenna on (Tx1RFEn, Tx2RFEn) */
}

RC522_Status RC522_CheckAlive(RC522_Handle *h)
{
    uint8_t v, tmode, txc;
    RC522_Status st;
    if ((st = reg_read(h, RC522_REG_VERSION, &v)) != RC522_OK) return st;
    if (v == 0x00U || v == 0xFFU || v != h->version) {
        return RC522_ERR_NO_CHIP;
    }
    /* A brown-out on the module resets it silently; detect lost configuration too. */
    if ((st = reg_read(h, RC522_REG_T_MODE, &tmode)) != RC522_OK) return st;
    if ((st = reg_read(h, RC522_REG_TX_CONTROL, &txc)) != RC522_OK) return st;
    if (tmode != 0x80U || (txc & 0x03U) != 0x03U) {
        return RC522_ERR_NO_CHIP;
    }
    return RC522_OK;
}

static RC522_Status select_level(RC522_Handle *h, uint8_t sel, uint8_t out4[4], uint8_t *sak)
{
    rx_frame rx;
    uint8_t anticoll[2] = { sel, PICC_NVB_ANTICOLL };
    RC522_Status st = transceive(h, anticoll, 2, 0, &rx);
    if (st != RC522_OK) {
        return st;
    }
    if (rx.len != 5U || rx.last_bits != 0U) {
        return RC522_ERR_PROTOCOL;
    }
    if ((uint8_t)(rx.buf[0] ^ rx.buf[1] ^ rx.buf[2] ^ rx.buf[3]) != rx.buf[4]) {
        return RC522_ERR_BCC;
    }

    uint8_t frame[9] = { sel, PICC_NVB_SELECT, rx.buf[0], rx.buf[1], rx.buf[2], rx.buf[3], rx.buf[4], 0, 0 };
    uint16_t crc = crc_a(frame, 7);
    frame[7] = (uint8_t)(crc & 0xFFU);
    frame[8] = (uint8_t)(crc >> 8);
    for (uint8_t i = 0; i < 4U; i++) {
        out4[i] = rx.buf[i];
    }

    st = transceive(h, frame, 9, 0, &rx);
    if (st != RC522_OK) {
        return st;
    }
    if (rx.len != 3U || rx.last_bits != 0U) {
        return RC522_ERR_PROTOCOL;
    }
    crc = crc_a(rx.buf, 1);
    if (rx.buf[1] != (uint8_t)(crc & 0xFFU) || rx.buf[2] != (uint8_t)(crc >> 8)) {
        return RC522_ERR_CRC;
    }
    *sak = rx.buf[0];
    return RC522_OK;
}

static void halt_card(RC522_Handle *h)
{
    uint8_t frame[4] = { PICC_CMD_HLTA, 0x00, 0, 0 };
    uint16_t crc = crc_a(frame, 2);
    frame[2] = (uint8_t)(crc & 0xFFU);
    frame[3] = (uint8_t)(crc >> 8);
    rx_frame rx;
    /* A halted card must stay silent: RC522_ERR_TIMEOUT is the expected outcome. */
    (void)transceive(h, frame, 4, 0, &rx);
}

RC522_Status RC522_ReadUid(RC522_Handle *h, RC522_Uid *out)
{
    if (h == NULL || out == NULL) {
        return RC522_ERR_PARAM;
    }
    RC522_Status st;
    uint8_t coll;
    /* ValuesAfterColl = 0: bits received after a collision are cleared (datasheet 9.3.1.15) */
    if ((st = reg_read(h, RC522_REG_COLL, &coll)) != RC522_OK) return st;
    if ((st = reg_write(h, RC522_REG_COLL, (uint8_t)(coll & 0x7FU))) != RC522_OK) return st;

    rx_frame rx;
    uint8_t reqa = PICC_CMD_REQA;
    st = transceive(h, &reqa, 1, 7, &rx);
    if (st == RC522_ERR_TIMEOUT) {
        return RC522_NO_CARD;
    }
    /* ATQA bits of different cards may collide; the anticollision step below will report it. */
    if (st != RC522_OK && st != RC522_ERR_COLLISION) {
        return st;
    }

    uint8_t part[4];
    uint8_t sak = 0;
    out->len = 0;

    st = select_level(h, PICC_CMD_SEL_CL1, part, &sak);
    if (st != RC522_OK) {
        return st;
    }
    if (part[0] == PICC_CASCADE_TAG) {
        out->uid[0] = part[1];
        out->uid[1] = part[2];
        out->uid[2] = part[3];
        st = select_level(h, PICC_CMD_SEL_CL2, part, &sak);
        if (st != RC522_OK) {
            return st;
        }
        if (part[0] == PICC_CASCADE_TAG || (sak & PICC_SAK_UID_INCOMPLETE)) {
            return RC522_ERR_UNSUPPORTED;
        }
        for (uint8_t i = 0; i < 4U; i++) {
            out->uid[3U + i] = part[i];
        }
        out->len = 7;
    } else {
        for (uint8_t i = 0; i < 4U; i++) {
            out->uid[i] = part[i];
        }
        out->len = 4;
    }
    out->sak = sak;
    halt_card(h);
    return RC522_OK;
}

const char *RC522_StatusStr(RC522_Status st)
{
    switch (st) {
    case RC522_OK:              return "OK";
    case RC522_NO_CARD:         return "NO_CARD";
    case RC522_ERR_TIMEOUT:     return "TIMEOUT";
    case RC522_ERR_COLLISION:   return "COLLISION";
    case RC522_ERR_CRC:         return "CRC";
    case RC522_ERR_PROTOCOL:    return "PROTOCOL";
    case RC522_ERR_BCC:         return "BCC";
    case RC522_ERR_UNSUPPORTED: return "UNSUPPORTED";
    case RC522_ERR_NO_CHIP:     return "NO_CHIP";
    case RC522_ERR_SPI:         return "SPI";
    case RC522_ERR_PARAM:       return "PARAM";
    default:                    return "?";
    }
}
