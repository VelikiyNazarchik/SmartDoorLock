#include "main.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"

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
