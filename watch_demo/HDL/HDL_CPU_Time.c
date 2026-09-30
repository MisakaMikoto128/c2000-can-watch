/**
 * @file HDL_CPU_Time.c
 * @brief CPU timer driver.
 *  - Timer0: 1 ms tick interrupt, drives millis tick.
 *  - Timer1: 32-bit free-running, used for microsecond timestamps.
 *  - Timer2: software single-shot timer.
 */
#include "HDL_CPU_Time.h"
#include "CPU_Define.h"
#include <limits.h>
#include <stddef.h>

#define CPU_FREQ_MHZ   100U  /* F280025 SYSCLK */
#define TICK_PERIOD_US 1000U /* Timer0 中断周期 = 1 ms */

static volatile uint32_t s_tick_ms = 0U;
static bool s_inited = false;

static volatile CPU_Time_Callback_t s_tick_callback = NULL;
static volatile CPU_Time_Callback_t s_hard_timer_callback = NULL;
static volatile bool s_hard_timer_busy = false;

__interrupt void HDL_CPU_Time_TickIsr(void);
__interrupt void HDL_CPU_Time_HardTimerIsr(void);

/* 定时器时钟 = SYSCLK / (TDDR + 1)，计数器从 PRD 递减到 0 触发一次。
 * 配完保持停止状态，由调用方决定何时清 TSS 启动。 */
static void ConfigTimer(volatile struct CPUTIMER_REGS *p_regs, uint16_t tddr, uint32_t prd,
                        bool int_en) {
    p_regs->TPRH.bit.TDDRH = (uint16_t)(tddr >> 8);
    p_regs->TPR.bit.TDDR = (uint16_t)(tddr & 0xFFU);
    p_regs->PRD.all = prd;
    /* 先停 */
    p_regs->TCR.bit.TSS = 1U;
    /* 装载 PRD */
    p_regs->TCR.bit.TRB = 1U;
    /* SOFT=FREE=1：仿真器暂停时定时器继续跑，否则断点里调 DelayMs 会永远出不来 */
    p_regs->TCR.bit.SOFT = 1U;
    p_regs->TCR.bit.FREE = 1U;
    p_regs->TCR.bit.TIE = int_en ? 1U : 0U;
}

void HDL_CPU_Time_Init(void) {
    if (s_inited) {
        return;
    }

    InitCpuTimers();

    EALLOW;
    PieVectTable.TIMER0_INT = &HDL_CPU_Time_TickIsr;
    PieVectTable.TIMER2_INT = &HDL_CPU_Time_HardTimerIsr;
    EDIS;

    ConfigTimer(&CpuTimer0Regs, CPU_FREQ_MHZ - 1U, TICK_PERIOD_US - 1U, true);
    ConfigTimer(&CpuTimer1Regs, CPU_FREQ_MHZ - 1U, 0xFFFFFFFFUL, false);
    ConfigTimer(&CpuTimer2Regs, CPU_FREQ_MHZ - 1U, 0xFFFFFFFFUL, false);

    s_tick_ms = 0U;
    s_tick_callback = NULL;
    s_hard_timer_callback = NULL;
    s_hard_timer_busy = false;

    CpuTimer0Regs.TCR.bit.TSS = 0U; /* 启动 ms tick */
    CpuTimer1Regs.TCR.bit.TSS = 0U; /* 启动 us 自由计数 */
    /* Timer2 保持停止，等 HDL_CPU_Time_StartHardTimer() */

    /* TIMER0 = PIE 组 1 的 INTx7。ADC 中断是 INTx1，组内序号小的优先级高，
     * 所以 40 kHz 控制中断始终压过这里的 1 kHz tick。 */
    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1U;

    /* Timer1/Timer2 直连 CPU INT13/INT14，不经 PIE，也不需要 PIEACK。
     * 这里开 IER 只是打开通路，实际由各自的 TIE 位控制。 */
    IER |= M_INT14;

    s_inited = true;
}

/* C28x 上 32 位读不是原子的，中断在两个 16 位半字之间打断会读到撕裂值
 * （每 65536 ms 一次进位跨界）。TODO 撕裂防护未启用：下方关中断/恢复两行
 * 被注释停用，当前为裸读 s_tick_ms。 */
uint32_t HDL_CPU_Time_GetTick(void) {
    uint32_t tick_ms;
    // uint16_t st1 = __disable_interrupts();
    tick_ms = s_tick_ms;
    // __restore_interrupts(st1);
    return tick_ms;
}

/* Timer1 是递减计数，取反得到递增的 us 计数 */
UsTimer_t HDL_CPU_Time_GetUsTick(void) {
    return ~CpuTimer1.RegsAddr->TIM.all;
}

void HDL_CPU_Time_ResetTick(void) {
    uint16_t st1 = __disable_interrupts();
    s_tick_ms = 0U;
    __restore_interrupts(st1);
}

void HDL_CPU_Time_ResetUsTick(void) {
    CpuTimer1Regs.TCR.bit.TRB = 1U; /* 重新装载 PRD，计数从头开始 */
}

void HDL_CPU_Time_DelayUs(UsTimer_t delay_us) {
    UsTimer_t start_us = HDL_CPU_Time_GetUsTick();
    while ((UsTimer_t)(HDL_CPU_Time_GetUsTick() - start_us) < delay_us) {
    }
}

void HDL_CPU_Time_DelayMs(uint32_t delay_ms) {
    uint32_t start_ms = HDL_CPU_Time_GetTick();
    while ((uint32_t)(HDL_CPU_Time_GetTick() - start_ms) < delay_ms) {
    }
}


bool HDL_CPU_Time_Elapsed(uint32_t *p_last_ms, uint32_t period_ms) {
    uint32_t now_ms = HDL_CPU_Time_GetTick();
    bool due = ((uint32_t)(now_ms - *p_last_ms) >= period_ms);
    if (due) {
        *p_last_ms = now_ms;
    }
    return due;
}

void HDL_CPU_Time_SetCPUTickCallback(CPU_Time_Callback_t p_callback) {
    s_tick_callback = p_callback;
}

bool HDL_CPU_Time_StartHardTimer(HDL_CPU_Time_HardTimerCh_t channel, UsTimer_t timeout_us,
                                 void *p_callback) {
    if (channel != HDL_CPU_TIME_HARD_TIMER_CH) {
        return false;
    }
    if (timeout_us == 0U) {
        return false;
    }
    /* 正忙就直接拒绝。 */
    // if (s_hard_timer_busy) {
    //     return false;
    // }

    // 每次调用都重配并重启，实现"每收到一字节复位 T3.5"的语义。
    s_hard_timer_callback = (CPU_Time_Callback_t)p_callback;
    s_hard_timer_busy = true;

    ConfigTimer(&CpuTimer2Regs, CPU_FREQ_MHZ - 1U, (uint32_t)timeout_us - 1U, true);
    CpuTimer2Regs.TCR.bit.TSS = 0U; /* 启动 */

    return true;
}

void HDL_CPU_Time_StopHardTimer(HDL_CPU_Time_HardTimerCh_t channel) {
    if (channel != HDL_CPU_TIME_HARD_TIMER_CH) {
        return;
    }
    CpuTimer2Regs.TCR.bit.TIE = 0U; /* 先关中断，再停表 */
    CpuTimer2Regs.TCR.bit.TSS = 1U;
    s_hard_timer_callback = NULL;
    s_hard_timer_busy = false;
}

/* 1 kHz，留在 Flash 即可，不必占 RAM */
__interrupt void HDL_CPU_Time_TickIsr(void) {
    CPU_Time_Callback_t p_cb = s_tick_callback;

    s_tick_ms++;
    CpuTimer0Regs.TCR.bit.TIF = 1U; /* 写 1 清中断标志 */

    if (p_cb != NULL) {
        p_cb();
    }

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}

/* 单次定时器到期。Timer2 直连 CPU INT14，不属于任何 PIE 组，不写 PIEACK。 */
__interrupt void HDL_CPU_Time_HardTimerIsr(void) {
    CPU_Time_Callback_t p_cb = s_hard_timer_callback;

    /* 先关中断、停表、清忙标志，再执行回调 —— 回调里可能立刻重启定时器 */
    CpuTimer2Regs.TCR.bit.TIE = 0U;
    CpuTimer2Regs.TCR.bit.TSS = 1U;
    CpuTimer2Regs.TCR.bit.TIF = 1U;

    s_hard_timer_callback = NULL;
    s_hard_timer_busy = false;

    if (p_cb != NULL) {
        p_cb();
    }
}
