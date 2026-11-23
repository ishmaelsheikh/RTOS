/**
 * @file    scheduler.c
 * @brief   Time-triggered cooperative scheduler, implementation.
 *          Target: NXP S32K344 (Cortex-M7), RTD 6.0.0.
 */
#include "scheduler.h"
#include "scheduler_cfg.h"
#include "Gpt.h"

#if (SCHED_INSTALL_IRQ_MANUALLY == STD_ON)
#include "IntCtrl_Ip.h"
extern void PIT_0_ISR(void);   /* handler provided by the RTD Gpt driver */
#endif

/* ------------------------------------------------------------------------- */
/* Cortex-M7 DWT cycle counter (core registers, same on every Cortex-M7)     */
/* ------------------------------------------------------------------------- */
#ifndef SCHED_DWT_CYCCNT   /* can be overridden for host (PC) unit tests */
#define SCHED_DEMCR        (*(volatile uint32 *)0xE000EDFCUL)
#define SCHED_DWT_CTRL     (*(volatile uint32 *)0xE0001000UL)
#define SCHED_DWT_CYCCNT   (*(volatile uint32 *)0xE0001004UL)
#define SCHED_DWT_LAR      (*(volatile uint32 *)0xE0001FB0UL)
#endif
#define SCHED_DEMCR_TRCENA (1UL << 24)
#define SCHED_DWT_CYCCNTENA (1UL << 0)

#define SCHED_CYC_PER_MS   (SCHED_CPU_HZ / 1000UL)

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */
static volatile uint32 Sched_Tick = 0U;     /* written only by the tick ISR  */

static uint32 Sched_AliveMask;              /* bit i set when task i has run  */
static uint32 Sched_AllMask;

static uint32 Sched_BusyCyc;                /* task cycles in current window  */
static uint32 Sched_WindowStart;
static uint32 Sched_LoadPermille;

/* ------------------------------------------------------------------------- */
static void Sched_DwtInit(void)
{
    SCHED_DEMCR   |= SCHED_DEMCR_TRCENA;
    SCHED_DWT_LAR  = 0xC5ACCE55UL;          /* unlock DWT (needed on some M7)  */
    SCHED_DWT_CYCCNT = 0U;
    SCHED_DWT_CTRL |= SCHED_DWT_CYCCNTENA;
}

void Sched_TickNotification(void)
{
    /* Interrupt context: keep it minimal. A 32-bit store is atomic on M7. */
    Sched_Tick++;
}

uint32 Sched_GetTick(void)
{
    return Sched_Tick;
}

uint32 Sched_GetCpuLoadPermille(void)
{
    return Sched_LoadPermille;
}

const Sched_TaskStats_t *Sched_GetTaskStats(uint32 idx)
{
    return (idx < Sched_TaskCount) ? &Sched_TaskTable[idx].stats : NULL_PTR;
}

void Sched_Init(void)
{
    uint32 i;

    Sched_DwtInit();

    Sched_Tick         = 0U;
    Sched_BusyCyc      = 0U;
    Sched_WindowStart  = 0U;
    Sched_LoadPermille = 0U;
    Sched_AliveMask    = 0U;
    Sched_AllMask      = 0U;

    for (i = 0U; (i < Sched_TaskCount) && (i < SCHED_MAX_TASKS); i++)
    {
        Sched_Task_t *t = &Sched_TaskTable[i];

        /* First release when tick == offset:
         *   (tick - lastRelease) >= period  <=>  tick >= offset
         * The unsigned underflow is intended (modulo 2^32 arithmetic). */
        t->lastRelease = t->offsetMs - t->periodMs;

        t->stats.runCount    = 0U;
        t->stats.lastExecCyc = 0U;
        t->stats.maxExecCyc  = 0U;
        t->stats.overruns    = 0U;

        Sched_AllMask |= (1UL << i);
    }
}

void Sched_Start(void)
{
#if (SCHED_INSTALL_IRQ_MANUALLY == STD_ON)
    IntCtrl_Ip_InstallHandler(PIT0_IRQn, PIT_0_ISR, NULL_PTR);
    IntCtrl_Ip_EnableIrq(PIT0_IRQn);
#endif
    Gpt_EnableNotification(SCHED_GPT_CHANNEL);
    Gpt_StartTimer(SCHED_GPT_CHANNEL, SCHED_GPT_TICKS);
}

/* Runs one due task (the first one in table order) and returns TRUE,
 * or returns FALSE if nothing is due. */
static boolean Sched_DispatchOne(uint32 now)
{
    uint32 i;

    for (i = 0U; (i < Sched_TaskCount) && (i < SCHED_MAX_TASKS); i++)
    {
        Sched_Task_t *t = &Sched_TaskTable[i];
        uint32 elapsed = now - t->lastRelease;          /* wrap-safe */

        if (elapsed >= t->periodMs)
        {
            uint32 c0;
            uint32 dt;

            if (elapsed >= (2UL * t->periodMs))
            {
                /* Missed at least one release: count it, then skip the
                 * missed runs instead of running them back-to-back.
                 * Phase (offset) is kept. */
                t->stats.overruns++;
                SCHED_OVERRUN_HOOK(i);
                t->lastRelease = now - (elapsed % t->periodMs);
            }
            else
            {
                /* Normal case: += period gives a drift-free rate. */
                t->lastRelease += t->periodMs;
            }

            c0 = SCHED_DWT_CYCCNT;
            t->fn();
            dt = SCHED_DWT_CYCCNT - c0;

            t->stats.runCount++;
            t->stats.lastExecCyc = dt;
            if (dt > t->stats.maxExecCyc)
            {
                t->stats.maxExecCyc = dt;
            }
            Sched_BusyCyc  += dt;
            Sched_AliveMask |= (1UL << i);

            return TRUE;
        }
    }
    return FALSE;
}

static void Sched_UpdateLoad(uint32 now)
{
    uint32 window = now - Sched_WindowStart;

    if (window >= SCHED_LOAD_WINDOW_MS)
    {
        /* Split the division so the 32-bit math cannot overflow:
         * busy <= 1.6e8, cycPerMs = 160000 -> busy/(cycPerMs/1000) <= 1e6 */
        uint32 totalUs = window * 1000UL;
        uint32 busyUs  = Sched_BusyCyc / (SCHED_CYC_PER_MS / 1000UL);

        Sched_LoadPermille = (busyUs >= totalUs) ? 1000UL
                                                 : (busyUs / (totalUs / 1000UL));
        Sched_BusyCyc     = 0U;
        Sched_WindowStart = now;
    }
}

void Sched_Run(void)
{
    for (;;)
    {
        uint32 now = Sched_Tick;

        /* Run one due task, then rescan from the top. A faster (higher
         * priority) task that became due meanwhile runs before slower ones. */
        if (Sched_DispatchOne(now) == FALSE)
        {
            Sched_UpdateLoad(now);

            if ((Sched_AliveMask & Sched_AllMask) == Sched_AllMask)
            {
                SCHED_WDG_SERVICE();
                Sched_AliveMask = 0U;
            }

            SCHED_IDLE_HOOK();
        }
    }
}
