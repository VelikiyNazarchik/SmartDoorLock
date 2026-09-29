#include "main.h"
#include "app.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"
#include "ui.h"

osMessageQueueId_t qCtrl;
osMessageQueueId_t qUi;
osMessageQueueId_t qLog;
osMessageQueueId_t qNetTx;
osTimerId_t        tmrState;

osThreadId_t tCtrlHandle;
osThreadId_t tRfidHandle;
osThreadId_t tInputHandle;
osThreadId_t tUiHandle;
osThreadId_t tNetHandle;
osThreadId_t tLogHandle;
osThreadId_t tSuperHandle;

bool App_PostCtrl(const AppEvent *ev)
{
    return qCtrl != NULL && osMessageQueuePut(qCtrl, ev, 0, 0) == osOK;
}

static void state_timer_cb(void *arg)
{
    (void)arg;
    AppEvent ev = { .type = EV_TIMEOUT };
    (void)App_PostCtrl(&ev);
}

static osThreadId_t start_task(osThreadFunc_t fn, const char *name, uint32_t stack_words, osPriority_t prio)
{
    const osThreadAttr_t attr = {
        .name = name,
        .stack_size = stack_words * 4U,
        .priority = prio,
    };
    osThreadId_t th = osThreadNew(fn, NULL, &attr);
    if (th == NULL) {
        Error_Handler();
    }
    return th;
}

static osMessageQueueId_t new_queue(const char *name, uint32_t len, uint32_t size)
{
    const osMessageQueueAttr_t attr = { .name = name };
    osMessageQueueId_t q = osMessageQueueNew(len, size, &attr);
    if (q == NULL) {
        Error_Handler();
    }
    return q;
}

void App_CreateRtosObjects(void)
{
    SUP_Init();

    qLog = new_queue("qLog", CFG_LOG_QUEUE_LEN, sizeof(LogMsg));
    LOG_Init(qLog);
    qCtrl = new_queue("qCtrl", CFG_QCTRL_LEN, sizeof(AppEvent));
    qUi = new_queue("qUi", CFG_QUI_LEN, sizeof(UiCommand));
    qNetTx = new_queue("qNetTx", CFG_QNET_LEN, sizeof(NetLine));

    static const osTimerAttr_t tattr = { .name = "tmrState" };
    tmrState = osTimerNew(state_timer_cb, osTimerOnce, NULL, &tattr);
    if (tmrState == NULL) {
        Error_Handler();
    }

    tSuperHandle = start_task(TaskSuper, "tSuper", CFG_STACK_SUPER, osPriorityRealtime);
    tCtrlHandle  = start_task(TaskCtrl,  "tCtrl",  CFG_STACK_CTRL,  osPriorityHigh);
    tRfidHandle  = start_task(TaskRfid,  "tRfid",  CFG_STACK_RFID,  osPriorityAboveNormal);
    tInputHandle = start_task(TaskInput, "tInput", CFG_STACK_INPUT, osPriorityNormal);
    tUiHandle    = start_task(TaskUi,    "tUi",    CFG_STACK_UI,    osPriorityBelowNormal);
    tNetHandle   = start_task(TaskNet,   "tNet",   CFG_STACK_NET,   osPriorityLow);
    tLogHandle   = start_task(TaskLog,   "tLog",   CFG_STACK_LOG,   osPriorityLow);

    SUP_Register(TASK_CTRL,  tCtrlHandle,  "tCtrl",  CFG_STACK_CTRL);
    SUP_Register(TASK_RFID,  tRfidHandle,  "tRfid",  CFG_STACK_RFID);
    SUP_Register(TASK_INPUT, tInputHandle, "tInput", CFG_STACK_INPUT);
    SUP_Register(TASK_UI,    tUiHandle,    "tUi",    CFG_STACK_UI);
    SUP_Register(TASK_NET,   tNetHandle,   "tNet",   CFG_STACK_NET);
    SUP_Register(TASK_LOG,   tLogHandle,   "tLog",   CFG_STACK_LOG);
}
