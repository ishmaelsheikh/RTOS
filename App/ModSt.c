/**
 * @file    ModSt.c
 * @brief   Example application module: system mode/state manager.
 *
 * State machine, stepped every 10 ms:
 *
 *   INIT --ModSt_Init()--> STARTUP --(100 ms)--> NORMAL
 *                             |                     |
 *                             +--ReportFault()------+--> FAULT (latched)
 *
 * In NORMAL the heartbeat LED blinks at 1 Hz; in FAULT it is on steadily.
 */
#include "ModSt.h"

/* STD_ON: drive a heartbeat LED through the RTD Dio driver.
 * Set the generated channel name (Dio_Cfg.h) below. */
#define MODST_USE_LED   STD_OFF

#if (MODST_USE_LED == STD_ON)
#include "Dio.h"
#define MODST_LED_CHANNEL   DioConf_DioChannel_Led
#define MODST_LED_WRITE(on) Dio_WriteChannel(MODST_LED_CHANNEL, (on) ? STD_HIGH : STD_LOW)
#else
#define MODST_LED_WRITE(on) do { (void)(on); } while (0)
#endif

static ModSt_State_t ModSt_State = MODST_STATE_INIT;
static uint32        ModSt_TimeInStateMs;
static boolean       ModSt_FaultRequest;
static boolean       ModSt_LedOn;

static void ModSt_Enter(ModSt_State_t next)
{
    ModSt_State         = next;
    ModSt_TimeInStateMs = 0U;
}

void ModSt_Init(void)
{
    ModSt_FaultRequest = FALSE;
    ModSt_LedOn        = FALSE;
    MODST_LED_WRITE(FALSE);
    ModSt_Enter(MODST_STATE_STARTUP);
}

void ModSt_Cyclic(void)
{
    /* Time is counted in calls * cycle time. No timer access needed, and
     * the module can be tested on a PC by simply calling it in a loop. */
    ModSt_TimeInStateMs += MODST_CYCLE_MS;

    if ((ModSt_FaultRequest == TRUE) && (ModSt_State != MODST_STATE_FAULT))
    {
        ModSt_Enter(MODST_STATE_FAULT);
    }

    switch (ModSt_State)
    {
        case MODST_STATE_STARTUP:
            /* Place startup checks here (first valid AFE/BJB data, ...). */
            if (ModSt_TimeInStateMs >= MODST_STARTUP_TIME_MS)
            {
                ModSt_Enter(MODST_STATE_NORMAL);
            }
            break;

        case MODST_STATE_NORMAL:
            if ((ModSt_TimeInStateMs % MODST_HEARTBEAT_MS) == 0U)
            {
                ModSt_LedOn = (ModSt_LedOn == TRUE) ? FALSE : TRUE;
                MODST_LED_WRITE(ModSt_LedOn);
            }
            break;

        case MODST_STATE_FAULT:
            ModSt_LedOn = TRUE;          /* steady on = fault */
            MODST_LED_WRITE(TRUE);
            break;

        case MODST_STATE_INIT:           /* Cyclic before Init: do nothing */
        default:
            break;
    }
}

void ModSt_ReportFault(void)
{
    ModSt_FaultRequest = TRUE;
}

ModSt_State_t ModSt_GetState(void)
{
    return ModSt_State;
}

uint32 ModSt_GetTimeInStateMs(void)
{
    return ModSt_TimeInStateMs;
}
