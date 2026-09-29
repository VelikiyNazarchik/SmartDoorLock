#ifndef RC522_REGS_H
#define RC522_REGS_H

/* MFRC522 datasheet rev. 3.9, section 9 "MFRC522 registers" */
#define RC522_REG_COMMAND        0x01U
#define RC522_REG_COM_IEN        0x02U
#define RC522_REG_DIV_IEN        0x03U
#define RC522_REG_COM_IRQ        0x04U
#define RC522_REG_DIV_IRQ        0x05U
#define RC522_REG_ERROR          0x06U
#define RC522_REG_STATUS1        0x07U
#define RC522_REG_STATUS2        0x08U
#define RC522_REG_FIFO_DATA      0x09U
#define RC522_REG_FIFO_LEVEL     0x0AU
#define RC522_REG_CONTROL        0x0CU
#define RC522_REG_BIT_FRAMING    0x0DU
#define RC522_REG_COLL           0x0EU
#define RC522_REG_MODE           0x11U
#define RC522_REG_TX_MODE        0x12U
#define RC522_REG_RX_MODE        0x13U
#define RC522_REG_TX_CONTROL     0x14U
#define RC522_REG_TX_ASK         0x15U
#define RC522_REG_CRC_RESULT_H   0x21U
#define RC522_REG_CRC_RESULT_L   0x22U
#define RC522_REG_MOD_WIDTH      0x24U
#define RC522_REG_RF_CFG         0x26U
#define RC522_REG_T_MODE         0x2AU
#define RC522_REG_T_PRESCALER    0x2BU
#define RC522_REG_T_RELOAD_H     0x2CU
#define RC522_REG_T_RELOAD_L     0x2DU
#define RC522_REG_VERSION        0x37U

/* Table 149 "Command overview" */
#define RC522_CMD_IDLE           0x00U
#define RC522_CMD_CALC_CRC       0x03U
#define RC522_CMD_TRANSCEIVE     0x0CU
#define RC522_CMD_SOFT_RESET     0x0FU

#define RC522_IRQ_TIMER          0x01U
#define RC522_IRQ_ERR            0x02U
#define RC522_IRQ_IDLE           0x10U
#define RC522_IRQ_RX             0x20U
#define RC522_DIVIRQ_CRC         0x04U
#define RC522_ERR_PROTOCOL_BIT   0x01U
#define RC522_ERR_PARITY_BIT     0x02U
#define RC522_ERR_CRC_BIT        0x04U
#define RC522_ERR_COLL_BIT       0x08U
#define RC522_ERR_BUF_OVFL_BIT   0x10U
#define RC522_COMMAND_POWER_DOWN 0x10U
#define RC522_BITFRAMING_START   0x80U
#define RC522_FIFO_FLUSH         0x80U

/* ISO/IEC 14443-3 PICC commands */
#define PICC_CMD_REQA            0x26U
#define PICC_CMD_SEL_CL1         0x93U
#define PICC_CMD_SEL_CL2         0x95U
#define PICC_CMD_SEL_CL3         0x97U
#define PICC_CMD_HLTA            0x50U
#define PICC_CASCADE_TAG         0x88U
#define PICC_NVB_ANTICOLL        0x20U
#define PICC_NVB_SELECT          0x70U
#define PICC_SAK_UID_INCOMPLETE  0x04U

#endif
