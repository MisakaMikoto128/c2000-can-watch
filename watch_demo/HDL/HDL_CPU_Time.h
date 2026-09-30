/* HDL_CPU_Time.h - CPU 定时器时基
 *
 * Timer0：1 kHz 中断，维护 ms tick，可挂一个每拍回调。
 * Timer1：1 MHz 自由计数，32 位，不开中断，提供 us 时基。
 * Timer2：单次硬件定时器，超时后执行回调。
 *
 * 接口与 DCVM 工程的同名模块保持一致，代码可互相移植。
 */
#ifndef USER_INCLUDE_HDL_CPU_TIME_H_
#define USER_INCLUDE_HDL_CPU_TIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "f28x_project.h"
#include <stdbool.h>
#include <stdint.h>

#define US_TIMER_BITWIDE 32 // 32或者16bit
#if US_TIMER_BITWIDE == 16
typedef uint16_t UsTimer_t;
#elif US_TIMER_BITWIDE == 32
typedef uint32_t UsTimer_t;
#endif

typedef void (*CPU_Time_Callback_t)(void);

typedef enum {
    HDL_CPU_TIME_HARD_TIMER_CH = 1,
} HDL_CPU_Time_HardTimerCh_t;

void HDL_CPU_Time_Init(void);
uint32_t HDL_CPU_Time_GetTick(void);
UsTimer_t HDL_CPU_Time_GetUsTick(void);
void HDL_CPU_Time_ResetTick(void);
void HDL_CPU_Time_ResetUsTick(void);
void HDL_CPU_Time_DelayUs(UsTimer_t delay_us);
void HDL_CPU_Time_DelayMs(uint32_t delay_ms);
bool HDL_CPU_Time_Elapsed(uint32_t *p_last_ms, uint32_t period_ms);
void HDL_CPU_Time_SetCPUTickCallback(CPU_Time_Callback_t p_callback);
bool HDL_CPU_Time_StartHardTimer(HDL_CPU_Time_HardTimerCh_t channel, UsTimer_t timeout_us,
                                 void *p_callback);
void HDL_CPU_Time_StopHardTimer(HDL_CPU_Time_HardTimerCh_t channel);

/* tick is 1 ms = 1000 us */
#define HDL_CPU_TIME_OEN_TICK_TIME 1000ULL

/**
 * @brief 将时间转换为tick,time单位为ms,
 *
 */
#define HDL_TIME_TO_TICK(_time) ((((_time) * 1000ULL) / HDL_CPU_TIME_OEN_TICK_TIME))

/**
 * @brief 将tick转换为时间,返回值单位为ms
 *
 */
#define HDL_TICK_TO_TIME(_tick) ((((_time) * HDL_CPU_TIME_OEN_TICK_TIME) / 1000ULL))

#define HDL_CPU_Time_GetUsTick_Diff(__last_tick)                                                   \
    ((UsTimer_t)(HDL_CPU_Time_GetUsTick() - (UsTimer_t)(__last_tick)))
#define MS_TO_TICK(_ms)   ((uint32_t)(_ms))
#define SEC_TO_TICK(_sec) ((uint32_t)(_sec) * 1000UL)
#define MIN_TO_TICK(_min) ((uint32_t)(_min) * 60UL * 1000UL)
#define HDL_CPU_Time_GetTick_Diff(__last_tick)                                                     \
    ((uint32_t)(HDL_CPU_Time_GetTick() - (uint32_t)(__last_tick)))
#define US_TICK(_us) ((UsTimer_t)(_us))
#ifdef __cplusplus
}
#endif

#endif /* USER_INCLUDE_HDL_CPU_TIME_H_ */
