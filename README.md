# Cooperative Scheduler for S32K344 — Bring-up Guide

Target: **NXP S32K344** · **RTD 6.0.0** · **S32 Design Studio (S32DS)**
Used with: **EVBMA7X18DT1** (BMA7418/BMA7118 cell controller AFE) and **EVBMA8420T** (BMA8420 battery junction box)

This guide takes you from an empty S32DS project to tasks running at verified rates on the board.
After that, you can add your own cyclic functions, as shown with `ModSt_Cyclic()`.

---

## Contents
1. [How it works](#1-how-it-works)
2. [Files](#2-files)
3. [Prerequisites](#3-prerequisites)
4. [Create the S32DS project](#4-create-the-s32ds-project)
5. [Configure RTD in S32 Config Tools](#5-configure-rtd-in-s32-config-tools)
6. [Add the scheduler to the project](#6-add-the-scheduler-to-the-project)
7. [Example: a 10 ms cyclic task (`ModSt_Cyclic`)](#7-example-a-10-ms-cyclic-task-modst_cyclic)
8. [Build, flash, run](#8-build-flash-run)
9. [Verify on the board](#9-verify-on-the-board)
10. [Connecting the AFE and BJB evaluation boards](#10-connecting-the-afe-and-bjb-evaluation-boards)
11. [Rules for task code](#11-rules-for-task-code)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. How it works

```
 PIT_0 (Gpt driver) ── every 1 ms ──► PIT_0_ISR (RTD) ──► Sched_TickNotification(): tick++
                                                              (only this runs in the ISR)
 main()
   ├─ RTD init: Mcu → Port → Platform → Gpt
   ├─ Application init: ModSt_Init(), ...
   ├─ Sched_Init(), Sched_Start()
   └─ Sched_Run()  ── forever ──►  is a task due?  yes → run it, then rescan from the top
                                                   no  → idle: load stats, watchdog, (WFI)
```

* **Cooperative, run to completion:** one task runs at a time and finishes before the next starts. You don't need mutexes or a stack per task.
* **Table order is priority:** if several tasks are due on the same tick, the one earlier in the table runs first.
* **Offset** is a task's first release time. Different offsets stop all tasks starting on the same tick.
  Task `{period 10, offset 1}` runs at 1, 11, 21, … ms.
* **Built-in checks:** worst-case run time per task (DWT cycle counter), overrun counter, CPU load, and a watchdog refreshed only after every task has run.
* **Interrupts still preempt tasks.** Keep ISRs short and do the processing in tasks.

---

## 2. Files

```
Scheduler/                       reusable core, normally no edits needed
  scheduler.h / scheduler.c      dispatcher, statistics, tick callback
  scheduler_cfg.h                ★ adapt to your project: Gpt channel name, clocks, hooks
App/                             your application
  tasks.c / tasks.h              ★ task table + task functions
  ModSt.c / ModSt.h              example module called every 10 ms
  main.c                         RTD init order + start-up
```

Start-up timeline of the example:

| Time | What happens |
|---|---|
| 0 ms | Scheduler starts; `ModSt` is in `STARTUP` |
| 1, 11, 21 … ms | `Task_10ms` → `ModSt_Cyclic()` |
| 100 ms | `ModSt` moves to `NORMAL` |
| every 500 ms | LED toggles (1 Hz blink) |
| on `ModSt_ReportFault()` | `ModSt` moves to `FAULT`; LED steady on, until reset |

---

## 3. Prerequisites

| Item | Notes |
|---|---|
| S32 Design Studio 3.5 or later | with the **S32K3 RTD 6.0.0** package installed (*Help → S32DS Extensions and Updates*) |
| S32K344 board | your MCU board, with its debug probe (on-board OpenSDA, PEmicro Multilink, Lauterbach, …) |
| Logic analyser or oscilloscope | to measure task timing on GPIO pins (strongly recommended) |
| For BMS work | EVBMA7X18DT1, EVBMA8420T, TPL transceiver/interface between the S32K344 and the daisy chain, TPL cables, lab supply or battery emulator |

> Start with the **S32K344 board alone**. Bring up the scheduler first (sections 4–9), and connect the AFE/BJB boards only after that (section 10).
> If something goes wrong, you then know which part caused it.

---

## 4. Create the S32DS project

1. **File → New → S32DS Application Project**
2. Processor: **S32K344**, core **Cortex-M7**. Toolchain: the default NXP GCC for Arm.
3. SDK: select the **RTD 6.0.0** SDK for S32K3.
4. Finish. S32DS creates `src/main.c`, the RTD sources, and a `.mex` configuration file.
5. Build the empty project once (**hammer icon**). It must build cleanly before you change anything.

---

## 5. Configure RTD in S32 Config Tools

Open the `.mex` file (double-click). It opens S32 Config Tools with the **Clocks**, **Pins** and **Peripherals** views.

### 5.1 Clocks
* Keep the default clock tree, or set your own.
* **Write down two values**; you'll need them in step 6.3:
  * `CORE_CLK`: CPU clock (default 160 MHz)
  * `AIPS_SLOW_CLK`: the clock that feeds PIT (default 40 MHz)

### 5.2 Pins
Route these pins as **GPIO outputs** (SIUL2, output buffer enabled):

| Signal | Purpose | Required? |
|---|---|---|
| `Led` | heartbeat LED for `ModSt` | recommended |
| `Dbg1ms`, `Dbg10ms`, `Dbg100ms` | toggled by each task, for measuring with a logic analyser | recommended for verification |

Use pins that your S32K344 board connects to an LED or a header (see the board's user manual or schematic).
Give each pin an identifier with exactly these names, so the generated Dio channel names match the code.

### 5.3 Peripherals (components)
Add or configure these components:

| Component | What to set |
|---|---|
| **Mcu** | Clock settings config `McuClockSettingConfig_0` and mode config `McuModeSettingConf_0` (default names).<br>In the mode settings' **peripheral list**, make sure **PIT_0** and **SIUL2** are **clock-enabled**. If PIT_0 has no clock, the first access to it causes a bus fault.<br>Add a **clock reference point** for the PIT clock (AIPS_SLOW_CLK). Gpt uses it. |
| **Port** | Generated from the Pins view; nothing extra. |
| **Dio** | Channels for the pins from 5.2 are created automatically. Names appear as `DioConf_DioChannel_<name>`. |
| **Platform** | Interrupt controller: enable **`PIT0_IRQn`**, handler **`PIT_0_ISR`**, priority e.g. 5. Leave the other interrupts as they are. |
| **Gpt** | General: enable **`GptEnableDisableNotificationApi`**.<br>Add a **channel**: hardware **PIT_0, channel 0**; mode **Continuous**; clock reference: the PIT clock reference point; **Notification = `Sched_TickNotification`**.<br>If the PIT has a "freeze in debug" option, enable it so the tick stops at breakpoints. |
| **OsIf** | Keep the default. If OsIf uses SysTick, that doesn't conflict: the scheduler uses PIT. |
| **Wdg** (optional, later) | Configure the SWT. See step 9.5. |

Then click **Update Code**. Afterwards, look in the generated `Gpt_Cfg.h` (or `Gpt_PBcfg.h`) for the channel's
symbolic name, e.g. `GptConf_GptChannelConfiguration_GptChannelConfiguration_0`.

---

## 6. Add the scheduler to the project

1. Copy `Scheduler/` and `App/` into the S32DS project's `src/` folder (refresh the project: **F5**).
2. Delete the generated `src/main.c`, or replace its contents with `App/main.c`. There can be only one `main()`.
3. **Edit `Scheduler/scheduler_cfg.h`:**

   ```c
   #define SCHED_GPT_CHANNEL   GptConf_GptChannelConfiguration_GptChannelConfiguration_0  /* from Gpt_Cfg.h  */
   #define SCHED_PIT_CLOCK_HZ  40000000UL    /* AIPS_SLOW_CLK from step 5.1 */
   #define SCHED_CPU_HZ        160000000UL   /* CORE_CLK from step 5.1      */
   ```
4. **Include paths:** *Project → Properties → C/C++ Build → Settings → Standard S32DS C Compiler → Includes*:
   add `"${ProjDirPath}/src/Scheduler"` and `"${ProjDirPath}/src/App"`.
5. **Enable debug pins and LED** (optional, recommended for the first run):
   * `App/tasks.c`: `#define TASKS_USE_DEBUG_PINS STD_ON`, and check the `DioConf_DioChannel_...` names.
   * `App/ModSt.c`: `#define MODST_USE_LED STD_ON`, and check `DioConf_DioChannel_Led`.
6. If your Mcu config names differ from the defaults, change them in `App/main.c`. If your project uses the
   **post-build** configuration variant, pass the generated config pointers instead of `NULL_PTR` to the `*_Init()` calls.

---

## 7. Example: a 10 ms cyclic task (`ModSt_Cyclic`)

This is the pattern for **every** function you want to call cyclically. `ModSt` is a small mode manager:

```
INIT ──ModSt_Init()──► STARTUP ──(100 ms)──► NORMAL (LED blinks 1 Hz)
                          │                      │
                          └──ModSt_ReportFault()─┴──► FAULT (LED on, latched)
```

### Step 1: the module provides an Init and a Cyclic function (`App/ModSt.h`)

```c
#define MODST_CYCLE_MS   10U     /* call period: must match the task it is placed in */

void ModSt_Init(void);           /* once, at start-up */
void ModSt_Cyclic(void);         /* every 10 ms       */
```

The module knows nothing about the scheduler. It counts time as `calls × MODST_CYCLE_MS`.
This means it can be moved to another task, or tested on a PC by calling it in a loop.

### Step 2: call Init once in `main()`, **before** the scheduler starts (`App/main.c`)

```c
    Gpt_Init(NULL_PTR);

    ModSt_Init();          /* all application Init() functions here */

    Sched_Init();
    Sched_Start();
    Sched_Run();           /* never returns */
```

### Step 3: call Cyclic inside the matching task (`App/tasks.c`)

```c
#include "ModSt.h"

void Task_10ms(void)
{
    DBG_TOGGLE(DBG_PIN_10MS);

    /* Call order = data-flow order: inputs -> processing -> outputs */
    ModSt_Cyclic();
    /* InputProc_Cyclic();  ... further 10 ms functions */
}
```

### Step 4 (only if you need a new rate): add a task to the table

```c
Sched_Task_t Sched_TaskTable[] =
{
    /*          name      fn           period  offset (ms) */
    SCHED_TASK("1ms",    Task_1ms,       1U,    0U),
    SCHED_TASK("10ms",   Task_10ms,     10U,    1U),
    SCHED_TASK("20ms",   Task_20ms,     20U,    5U),   /* <- new: also declare it in tasks.h */
    SCHED_TASK("100ms",  Task_100ms,   100U,    3U),
    SCHED_TASK("1000ms", Task_1000ms, 1000U,    7U),
};
```
Put faster tasks first in the table, and give each a different offset.

### Step 5: check its time budget
After running, look at `Sched_TaskTable[1].stats.maxExecCyc` in the debugger (index 1 = `Task_10ms`).
At 160 MHz: **µs = cycles / 160**. Keep every task well **below 1 ms**. The 1 ms task can't start until the running task finishes,
so a long task delays it.

---

## 8. Build, flash, run

1. **Build** (hammer). Target: **0 errors, 0 warnings**.
   * Error `GptConf_... undeclared`: fix `SCHED_GPT_CHANNEL` (step 6.3).
   * Error `undefined reference to Sched_TickNotification`: `scheduler.c` isn't being compiled. Make sure the folder is not excluded from the build.
2. **Debug**: *Run → Debug Configurations*. Pick the configuration for your probe (e.g. *PEmicro*), then **Debug**.
3. Execution stops at `main()`. Press **Resume (F8)**.
4. Expected: the LED starts blinking at **1 Hz** about 100 ms after reset.

---

## 9. Verify on the board

Do the steps in this order. Each one relies on the previous one passing.

### 9.1 Tick is running
Pause the debugger and add a watch on `Sched_Tick`. Resume, pause again: the value must have grown by about **1000 per second**.

### 9.2 Task periods and offsets (logic analyser)
With `TASKS_USE_DEBUG_PINS = STD_ON`, each pin **toggles** once per run, so a full square-wave period is two task periods:

| Pin | Expected square wave | Pass if |
|---|---|---|
| `Dbg1ms` | 2 ms period (500 Hz) | edges every 1.000 ms ± your jitter limit (e.g. ±20 µs) |
| `Dbg10ms` | 20 ms period | edges every 10.000 ms; each edge ~1 ms after a `Dbg1ms` edge (offset 1) |
| `Dbg100ms` | 200 ms period | edges every 100.000 ms, ~3 ms after an edge at a multiple of 10 ms |
| `Led` | 1 s period | toggles every 500 ms |

### 9.3 Statistics (debugger watch expressions)

| Expression | Expected |
|---|---|
| `Sched_TaskTable[i].stats.runCount` | increases at the task's rate |
| `Sched_TaskTable[i].stats.overruns` | **0** |
| `Sched_TaskTable[i].stats.maxExecCyc / 160` | worst-case run time in µs, within your budget |
| `Sched_GetCpuLoadPermille()` (or `Sched_LoadPermille`) | CPU load in 0.1 % steps, within your limit |
| `ModSt_State` | `MODST_STATE_NORMAL` |

### 9.4 Overrun detection works (fault injection)
Add a temporary busy-loop of about 25 ms to `Task_100ms`. After it runs, `overruns` must increase for `Task_1ms` and `Task_10ms`.
Afterwards, the tasks must return to their normal timing on the logic analyser. Remove the busy-loop again.

### 9.5 Watchdog (once the SWT is configured)
Put the watchdog refresh into `SCHED_WDG_SERVICE()` in `scheduler_cfg.h`. Then add a temporary `for(;;){}` to one task:
the MCU must **reset** within the watchdog timeout. The watchdog timeout must be longer than the slowest task period (1000 ms here).

### 9.6 Soak test
Run for several hours (ideally 24 h). Afterwards, `overruns` must still be 0 and `maxExecCyc` stable.

---

## 10. Connecting the AFE and BJB evaluation boards

Do this only after section 9 passes.

```
 S32K344 ──LPSPI──► TPL transceiver ══TPL (isolated daisy chain)══► EVBMA7X18DT1 (AFE) ══► EVBMA8420T (BJB)
```

1. **Hardware:** follow NXP's *Getting Started* guides for both boards: power (lab supply or battery emulator), TPL cables, jumpers.
   Check the boards first with NXP's evaluation GUI, so you know the chain works before your own firmware talks to it.
2. **LPSPI:** configure the LPSPI instance connected to the TPL transceiver in S32 Config Tools (Spi component + pins).
3. **Driver:** use NXP's BMS driver package for the BMA7xxx/BMA8420 if it's available for your RTD version, or your own register driver.
4. **Fit it into the scheduler:**
   * Wrap the AFE and BJB communication in modules with `Xxx_Init()` / `Xxx_Cyclic()`, exactly like `ModSt` (section 7).
   * **Never wait for an SPI/TPL transfer inside a task.** Start it with `Spi_AsyncTransmit()` (ideally with DMA), return,
     and handle the result on the next call (a small state machine: `IDLE → REQUEST → WAIT → DONE`).
     A blocking read of the whole chain can take several ms, which will cause overruns in the 1 ms task.
   * Use offsets to space trigger and read: e.g. trigger a measurement in one task, read it in a task with a later offset.
   * Buffers used by DMA must be in **non-cacheable** RAM (the Cortex-M7 D-cache doesn't see DMA writes).
5. **Typical rates** (starting point; use your own requirements):

   | Task | Typical BMS work |
   |---|---|
   | 1 ms | BJB current sampling, overcurrent check, step the TPL communication state machine |
   | 10 ms | cell voltage/temperature processing, fault state machine, `ModSt_Cyclic()` |
   | 100 ms | state of charge, balancing decisions, CAN reporting |
   | 1000 ms | diagnostics, heartbeat, statistics output |

6. **Verify again:** run section 9 with the chain connected. Also check measured cell voltages against a DMM or the battery emulator
   settings, current against a reference, and that TPL communication error counters stay at 0.

---

## 11. Rules for task code

| ✅ Do | ❌ Don't |
|---|---|
| Return quickly (well under 1 ms) | Busy-wait for hardware (`while(!flag){}`), delay loops |
| Use a state machine for anything that takes time | Call blocking driver functions inside a task |
| Keep state in `static` variables in the module | Call `Sched_Run()` or anything that never returns |
| Call `Init()` once from `main()` before `Sched_Start()` | Call `Init()` from inside a task |
| Mark data shared with an ISR `volatile` | Share data wider than 32 bits with an ISR without disabling interrupts briefly |
| Order calls inside a task by data flow | Assume a module runs before another one unless the order is written that way |

---

## 12. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Sched_Tick` stays 0, no task runs | Notification not enabled; IRQ not enabled; wrong channel | Check `GptEnableDisableNotificationApi`, the notification name `Sched_TickNotification`, `PIT0_IRQn` enabled in Platform, `SCHED_GPT_CHANNEL` |
| HardFault / BusFault at `Gpt_Init` | PIT_0 not clock-enabled | Enable PIT_0 in the Mcu mode settings' peripheral list |
| Stuck in `while (Mcu_GetPllStatus...)` | PLL not configured or not locking | Check the Clocks view; use the default clock config first |
| Tasks run 2.5× or 4× too fast or slow | `SCHED_PIT_CLOCK_HZ` doesn't match AIPS_SLOW_CLK | Correct the value in `scheduler_cfg.h` |
| `maxExecCyc` values look wrong | `SCHED_CPU_HZ` doesn't match CORE_CLK, or the debugger halted mid-task | Correct the value; ignore runs where you hit a breakpoint |
| `overruns` increasing | A task takes too long | Find the task with the largest `maxExecCyc`; split its work or make it non-blocking |
| LED doesn't blink, but tasks run | `MODST_USE_LED` off, wrong Dio channel name, pin not GPIO output | Check steps 5.2 and 6.5 |
| Ticks jump while debugging | PIT keeps running while the core is halted | Enable PIT freeze-in-debug, or ignore statistics after breakpoints |

---

### Verified so far (on PC, not yet on the board)
* All sources compile with `gcc -std=c99 -Wall -Wextra -Wpedantic` against stand-in RTD headers, with no warnings.
* Host test of the scheduler: offsets, periods, overrun recovery with phase kept, CPU load (12.0 % for a known load), and the watchdog alive mask.
* Host test of `ModSt`: `STARTUP` → `NORMAL` at 100 ms, 1 Hz LED toggling, `FAULT` latched.
* **Not yet done:** build with the real RTD 6.0.0 headers in S32DS, and all steps of section 9 on the board.
