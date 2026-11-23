/**
 * @file    main.c
 * @brief   Example integration of the scheduler into an S32DS RTD 6.0.0
 *          project for S32K344. Replace the main.c of your S32DS project,
 *          or copy the body of main() into it.
 *
 * Config names below (McuClockSettingConfig_0, McuModeSettingConf_0) are the
 * S32 Config Tools defaults. If you renamed them, check Mcu_Cfg.h.
 */
#include "Mcu.h"
#include "Port.h"
#include "Platform.h"
#include "Gpt.h"
#include "scheduler.h"

int main(void)
{
    /* 1. Clocks */
    Mcu_Init(NULL_PTR);
    Mcu_InitClock(McuClockSettingConfig_0);
#if (MCU_NO_PLL == STD_OFF)
    while (MCU_PLL_LOCKED != Mcu_GetPllStatus())
    {
        /* wait for PLL lock */
    }
    Mcu_DistributePllClock();
#endif
    Mcu_SetMode(McuModeSettingConf_0);

    /* 2. Pins */
    Port_Init(NULL_PTR);

    /* 3. Interrupt controller (enables PIT0 IRQ if configured in Platform) */
    Platform_Init(NULL_PTR);

    /* 4. Other drivers used by your tasks go here (Dio needs no init;
     *    Adc_Init, Can_Init, Wdg_Init, ...) */

    /* 5. Tick timer driver */
    Gpt_Init(NULL_PTR);

    /* 6. Scheduler */
    Sched_Init();
    Sched_Start();
    Sched_Run();        /* never returns */

    return 0;
}
