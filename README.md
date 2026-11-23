# Cooperative scheduler for S32K344 (RTD 6.0.0, S32 Design Studio)

```
Scheduler/  scheduler.c/.h, scheduler_cfg.h   <- reusable core
App/        tasks.c/.h (task table), main.c   <- example integration
```

## Add to an S32DS project
1. Copy `Scheduler/` and `App/` into the project's `src/`. Use `App/main.c` in place of the
   generated `main.c`, or copy its body into it.
2. Add both folders to *Project > Properties > C/C++ Build > Settings > Include paths*.

## S32 Config Tools (Peripherals)
| Component | Setting |
|-----------|---------|
| **Gpt** | Add a channel on **PIT_0** (FTM/STM also work). Channel mode **Continuous**. Notification: `Sched_TickNotification`. Enable `GptEnableDisableNotificationApi`. |
| **Platform → Interrupt Controller** | Enable **PIT0_IRQn**, handler **PIT_0_ISR**, priority e.g. 5. (Or set `SCHED_INSTALL_IRQ_MANUALLY` to `STD_ON`.) |
| **Clocks** | Note the PIT clock (AIPS_SLOW_CLK, 40 MHz by default) and CORE_CLK (160 MHz). |
| **Mcu** | Clock and mode config names must match `main.c` (`McuClockSettingConfig_0`, `McuModeSettingConf_0`). |

Then **Update Code**, and check in `scheduler_cfg.h`:
- `SCHED_GPT_CHANNEL` matches the name in the generated `Gpt_Cfg.h`
- `SCHED_PIT_CLOCK_HZ` and `SCHED_CPU_HZ` match the Clocks tool

## Adding a task
Write a `void fn(void)` that runs to completion, then add a line to `Sched_TaskTable[]` in
`App/tasks.c`:
```c
SCHED_TASK("myTask", MyTask, 20U /*period ms*/, 5U /*offset ms*/),
```
Table order is priority. Give tasks different offsets so they don't all start on the same tick.

## Debugging and timing
- Watch `Sched_TaskTable[i].stats` in the debugger. `maxExecCyc / 160` gives the worst-case run time in µs.
- `Sched_GetCpuLoadPermille()` returns CPU load in 0.1 % steps.
- Set `TASKS_USE_DEBUG_PINS` to `STD_ON` in `tasks.c` to toggle Dio pins and check timing on a scope.
- Put the watchdog refresh in `SCHED_WDG_SERVICE()`. It runs only after every task has run.
