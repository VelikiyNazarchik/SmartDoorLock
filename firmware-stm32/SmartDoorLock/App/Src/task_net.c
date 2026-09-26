#include "main.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"

/* tNet: owner of USART1 and the ESP module. Until the ESP link (milestone M5) exists it keeps the
 * ESP in reset and drains the outgoing queue, so tCtrl can already publish its events. */

void TaskNet(void *arg)
{
    (void)arg;
    HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_RESET);
    LOG_INFO("network link disabled (planned for M5), events are only logged");

    NetLine line;
    for (;;) {
        SUP_Heartbeat(TASK_NET);
        if (osMessageQueueGet(qNetTx, &line, NULL, 500) == osOK) {
            LOG_DEBUG("net <- %s", line.line);
        }
    }
}
