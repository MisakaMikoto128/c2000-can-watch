/* main.c - WATCH 演示固件：维护一组演示变量 + CAN 命令应答。
 *
 * 主循环每毫秒更新一次演示变量（计数器、运行时间、正弦波、演示状态结构体），
 * 上位机 WATCH 页通过 0x30~0x35 命令读写这些变量，演示运行时变量观测。
 */
#include <math.h>
#include "f28x_project.h"
#include "HDL_CAN.h"
#include "HDL_CPU_Time.h"
#include "can_app.h"
#include "app_config.h"

/* 演示观察变量（volatile：上位机写与主循环并发访问） */
volatile uint32_t g_demo_counter;   /* 每毫秒自增的计数器 */
volatile uint32_t g_demo_uptime_ms; /* 上电起的毫秒数 */
volatile float g_demo_wave;         /* 1 Hz 正弦演示值，幅值 = g_demo_setpoint */
volatile float g_demo_setpoint = 2.0f; /* 正弦幅值，可从上位机写入改波形 */
typedef struct {
    uint16_t mode;
    float gain;
    float bias;
    uint32_t ticks;
} Demo_Status_t;
volatile Demo_Status_t g_demo_status = {1U, 1.0f, 0.0f, 0U};

int main(void)
{
    InitSysCtrl();
    InitGpio();
    DINT;
    InitPieCtrl();
    IER = 0U;
    IFR = 0U;
    InitPieVectTable();
    HDL_CPU_Time_Init();
    EINT;
    ERTM;

    HDL_CAN_Init(CAN_BAUD_125K);

    uint32_t last_ms = HDL_CPU_Time_GetTick();
    for (;;) {
        can_app_poll();

        const uint32_t now = HDL_CPU_Time_GetTick();
        if (now != last_ms) {
            g_demo_uptime_ms = now;
            g_demo_counter += (now - last_ms);
            /* 1 Hz 正弦：相位由运行时间取模得到，不累积浮点误差 */
            g_demo_wave = g_demo_setpoint *
                          sinf((float)(now % 1000U) * (6.2831853f / 1000.0f));
            g_demo_status.ticks = g_demo_counter;
            last_ms = now;
        }
    }
}
