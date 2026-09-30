/**
* @file can_dbg_rd.h
* @brief 调试内存读事务：0x30 受理（快照/直读）→ 0x31 流式发送。
*
* 单槽：受理即覆盖在途事务，被打断的读不另行通知，残留帧由上位机按
* 0x31 的序号回显过滤。与写事务的互斥由调度层负责：受理 0x30 前先调
* can_dbg_wr_abort()。
*/
#ifndef __CAN_DBG_RD_H
#define __CAN_DBG_RD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "HDL_CAN.h"

/* 单次读取字数上限（0x30 B4），快照缓冲按此定容 */
#define CAN_DBG_RD_MAX_WORDS 255U

/** @brief 受理一帧读开始（cmd 0x30）。地址/长度非法的帧整帧忽略。 */
void can_dbg_rd_start(const HDL_CAN_Frame_t *f);
/** @brief 流发送推进：按帧间隔发 0x31，主循环调用。 */
void can_dbg_rd_periodic(void);
/** @brief 作废在途读事务。 */
void can_dbg_rd_abort(void);

#ifdef __cplusplus
}
#endif
#endif /* __CAN_DBG_RD_H */
