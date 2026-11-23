/**
 * @file    scheduler.h
 * @brief   Time-triggered cooperative (run-to-completion) scheduler.
 *          Target: NXP S32K344, RTD 6.0.0.
 *
 * Usage:
 *   1. Define Sched_TaskTable[] / Sched_TaskCount in your application
 *      (see App/tasks.c). Order = priority: earlier entries win ties.
 *   2. After all RTD drivers are initialised: Sched_Init(); Sched_Start();
 *   3. Sched_Run();   // never returns
 *   4. In S32 Config Tools set the Gpt channel notification to
 *      Sched_TickNotification.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Std_Types.h"
#include "scheduler_cfg.h"

/** Task function: must run to completion and never block. */
typedef void (*Sched_TaskFn_t)(void);

/** Runtime statistics per task (read-only for the application). */
typedef struct
{
    uint32 runCount;       /**< number of completed runs                       */
    uint32 lastExecCyc;    /**< execution time of last run, CPU cycles         */
    uint32 maxExecCyc;     /**< worst observed execution time, CPU cycles      */
    uint32 overruns;       /**< times the task missed one or more releases     */
} Sched_TaskStats_t;

/** One task table entry. Fill name/fn/periodMs/offsetMs; zero-init the rest. */
typedef struct
{
    const char        *name;       /**< for debugging only                      */
    Sched_TaskFn_t     fn;         /**< function to call                        */
    uint32             periodMs;   /**< release period, multiple of tick, > 0   */
    uint32             offsetMs;   /**< first release time; spreads load        */
    /* ---- internal ---- */
    uint32             lastRelease;
    Sched_TaskStats_t  stats;
} Sched_Task_t;

/** Helper to define a task table entry with all internal fields zeroed. */
#define SCHED_TASK(name, fn, periodMs, offsetMs) \
    { (name), (fn), (periodMs), (offsetMs), 0U, { 0U, 0U, 0U, 0U } }

/* Provided by the application (App/tasks.c) */
extern Sched_Task_t  Sched_TaskTable[];
extern const uint32  Sched_TaskCount;

/** Initialise scheduler state and the DWT cycle counter.
 *  Call after Gpt_Init() and before Sched_Start(). */
void   Sched_Init(void);

/** Start the tick timer. Tasks are released from this point on. */
void   Sched_Start(void);

/** Dispatcher loop, never returns. */
void   Sched_Run(void);

/** Gpt notification callback. Set this name as the channel's
 *  "Notification" in the Gpt component. Runs in interrupt context. */
void   Sched_TickNotification(void);

/** Current tick count (ms since Sched_Start). */
uint32 Sched_GetTick(void);

/** CPU load of the last completed window in 0.1 % units (0..1000). */
uint32 Sched_GetCpuLoadPermille(void);

/** Statistics of task idx, or NULL_PTR if idx is out of range. */
const Sched_TaskStats_t *Sched_GetTaskStats(uint32 idx);

/** Convert CPU cycles to microseconds (for printing statistics). */
#define SCHED_CYC_TO_US(cyc)   ((cyc) / (SCHED_CPU_HZ / 1000000UL))

#endif /* SCHEDULER_H */
