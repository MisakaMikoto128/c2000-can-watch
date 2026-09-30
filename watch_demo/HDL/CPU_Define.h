/**
 * @file CPU_Define.h
 * @author Liu Yuanlin
 * @brief
 * @version 0.1
 * @date 2024-04-17
 * @last modified 2024-04-17
 *
 * @copyright Copyright (c) 2024 Liu Yuanlin (CC BY-NC-SA 4.0).
 *
 */
#ifndef CPU_DEFINE_H
#define CPU_DEFINE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>

#include "f28x_project.h"

#define DISABLE_INT() _disable_interrupts()
#define ENABLE_INT() _enable_interrupts()

#ifdef __cplusplus
}
#endif
#endif //! CPU_DEFINE_H
