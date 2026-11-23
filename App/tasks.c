/**
 * @file    tasks.c
 * @brief   Task table + application tasks.
 *
 * Rules for every task:
 *   - run to completion, never block / busy-wait on hardware for long
 *   - worst-case execution time << period of the fastest task
 *     (check Sched_GetTaskStats(i)->maxExecCyc in the debugger)
 *   - keep state in static variables; a task is just a function call
 */
#include "Std_Types.h"
#include "scheduler.h"
#include "tasks.h"

/* Set to STD_ON to toggle a pin in each task for scope measurements.
 * Configure the pins as Dio channels in S32 Config Tools and put the
 * generated DioConf_DioChannel_... names below. */
#define TASKS_USE_DEBUG_PINS   STD_OFF

#if (TASKS_USE_DEBUG_PINS == STD_ON)
#include "Dio.h"
#define DBG_PIN_1MS     DioConf_DioChannel_Dbg1ms
#define DBG_PIN_10MS    DioConf_DioChannel_Dbg10ms
#define DBG_PIN_100MS   DioConf_DioChannel_Dbg100ms
#define DBG_PIN_1000MS  DioConf_DioChannel_Led
#define DBG_TOGGLE(ch)  ((void)Dio_FlipChannel(ch))
#else
#define DBG_TOGGLE(ch)  do { } while (0)
#endif

/* ------------------------------------------------------------------------- */
/* Task table. Order = priority (first entry wins when several are due).    */
/* Offsets spread the tasks so they do not all fire on the same tick.        */
/* ------------------------------------------------------------------------- */
Sched_Task_t Sched_TaskTable[] =
{
    /*          name      fn           period  offset (ms) */
    SCHED_TASK("1ms",    Task_1ms,       1U,    0U),
    SCHED_TASK("10ms",   Task_10ms,     10U,    1U),
    SCHED_TASK("100ms",  Task_100ms,   100U,    3U),
    SCHED_TASK("1000ms", Task_1000ms, 1000U,    7U),
};

const uint32 Sched_TaskCount = sizeof(Sched_TaskTable) / sizeof(Sched_TaskTable[0]);

/* ------------------------------------------------------------------------- */
/* Tasks                                                                     */
/* ------------------------------------------------------------------------- */
void Task_1ms(void)
{
    DBG_TOGGLE(DBG_PIN_1MS);
    /* e.g. fast control loop, read ADC results written by the ADC ISR */
}

void Task_10ms(void)
{
    DBG_TOGGLE(DBG_PIN_10MS);
    /* e.g. CAN signal processing, debouncing inputs */
}

void Task_100ms(void)
{
    DBG_TOGGLE(DBG_PIN_100MS);
    /* e.g. state machines, diagnostics */
}

void Task_1000ms(void)
{
    DBG_TOGGLE(DBG_PIN_1000MS);   /* 0.5 Hz heartbeat LED */
    /* e.g. housekeeping, log Sched_GetCpuLoadPermille() */
}
