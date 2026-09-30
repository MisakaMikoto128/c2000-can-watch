/*
 * units.h
 *
 * 物理单位标注宏。
 */

#ifndef _UNITS_H_
#define _UNITS_H_

#include <stdbool.h>
#include <stdint.h>

/* 电压 */
#define V(x)    ((x))
#define mV(x)   ((x))
#define kV(x)   ((x))

/* 电流 */
#define A(x)    ((x))
#define mA(x)   ((x))

/* 功率 */
#define W(x)    ((x))
#define kW(x)   ((x))

/* 电阻 */
#define OHM(x)  ((x))
#define kOHM(x) ((x))

/* 频率 */
#define Hz(x)   ((x))
#define kHz(x)  ((x))
#define MHz(x)  ((x))

/* 时间 */
#define sec(x)    ((x))
#define msec(x)   ((x))
#define usec(x)   ((x))
#define nsec(x)   ((x))

/* 温度，摄氏度 */
#define degc(x) ((x))

#endif /* _UNITS_H_ */
