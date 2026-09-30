/**
* @file can_dbg_wr.h
* @brief 调试内存写事务：0x32 受理校验 → 等待 0x33 → 关中断写入 → 0x34 终态。
*
* 受理即校验（字数、Flash 区），非法立即回 0x34；合法进入写等待，收到
* 0x33 后关中断写入并回 0x34，写方向只有这一帧终态回复。写等待被任一新
* 请求打断时静默作废、不发 0x34（上位机刚发的新请求马上要收新事务的
* 回复，旧的失败帧只会添乱），由调度层在受理 0x32 前先调 can_dbg_rd_abort()。
*/
#ifndef __CAN_DBG_WR_H
#define __CAN_DBG_WR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "HDL_CAN.h"

/* 单次写入字数上限（0x32 B4），0x33 载荷 DLC = 字数×2 */
#define CAN_DBG_WR_MAX_WORDS 4U

/** @brief 受理一帧写开始（cmd 0x32）。字数或地址非法时立即回 0x34 拒绝。 */
void can_dbg_wr_start(const HDL_CAN_Frame_t *f);
/** @brief 处理一帧写数据（cmd 0x33）。无在途事务时静默丢弃。 */
void can_dbg_wr_data(const HDL_CAN_Frame_t *f);
/** @brief 写等待超时检查，主循环调用；超时回 0x34 状态 3。 */
void can_dbg_wr_periodic(void);
/** @brief 作废在途写事务（不回复）。 */
void can_dbg_wr_abort(void);

#ifdef __cplusplus
}
#endif
#endif /* __CAN_DBG_WR_H */
