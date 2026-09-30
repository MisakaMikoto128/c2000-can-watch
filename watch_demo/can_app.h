/* can_app.h - 应用侧自有协议（dev 0x0C 段）：收发调度与命令实现。 */
#ifndef __CAN_APP_H
#define __CAN_APP_H

#include <stdint.h>
#include "HDL_CAN.h"
#include "can_dev.h"

/** @brief 主循环调用：收发各推进一次，把 dev 0x0C 段的帧按 cmd 分发。 */
void can_app_poll(void);

#endif /* __CAN_APP_H */
