/**
 * @file    ModSt.h
 * @brief   Example application module: system mode/state manager.
 *
 * Shows the typical pattern of a module driven by the scheduler:
 *   - ModSt_Init()   called once from main() before Sched_Start()
 *   - ModSt_Cyclic() called every MODST_CYCLE_MS from Task_10ms()
 *   - getters for other modules; no knowledge of the scheduler itself
 */
#ifndef MODST_H
#define MODST_H

#include "Std_Types.h"

/** Call period of ModSt_Cyclic(). Must match the task it is placed in. */
#define MODST_CYCLE_MS          10U

/** Time spent in STARTUP before going to NORMAL (e.g. wait for AFE/BJB
 *  to report first valid measurements). */
#define MODST_STARTUP_TIME_MS   100U

/** Heartbeat LED half-period in NORMAL state. */
#define MODST_HEARTBEAT_MS      500U

typedef enum
{
    MODST_STATE_INIT = 0,   /**< before ModSt_Init()                       */
    MODST_STATE_STARTUP,    /**< initialised, waiting for startup checks   */
    MODST_STATE_NORMAL,     /**< normal operation                          */
    MODST_STATE_FAULT       /**< fault latched, outputs in safe state      */
} ModSt_State_t;

void          ModSt_Init(void);
void          ModSt_Cyclic(void);

/** Request a transition to FAULT (latched until reset). Safe to call from
 *  any task; do not call from an ISR. */
void          ModSt_ReportFault(void);

ModSt_State_t ModSt_GetState(void);
uint32        ModSt_GetTimeInStateMs(void);

#endif /* MODST_H */
