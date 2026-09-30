/**
* @file can_dbg.h
* @brief 调试内存读写协议公共定义（dev 0x0C cmd 0x30~0x35）。
*
* 读事务与写事务分属 can_dbg_rd / can_dbg_wr 两个模块，CRC 校验在
* can_dbg_crc（无事务状态），本文件只放共用的命令号、状态码与帧 ID 组装。
* 整组命令由 CAN_DBG_ENABLE 守门（app_config.h）：关闭时各接口在本文件
* 内联为空实现，调用点无条件调用、不出现条件编译。
*/
#ifndef __CAN_DBG_H
#define __CAN_DBG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "HDL_CAN.h"
#include "can_dev.h"
#include "app_config.h"
#include "ccommon.h" /* byte_t：C28x 无 8 位类型，字节语义统一为 16 位（LIBs 层） */

/* cmd 号（dev 0x0C 段内，cmd 域 6 位） */
#define CAN_DBG_CMD_RD_START  0x30U /* 读开始（上位机→模块，仅单播） */
#define CAN_DBG_CMD_RD_DATA   0x31U /* 读数据（模块→上位机，流式多帧） */
#define CAN_DBG_CMD_WR_START  0x32U /* 写开始（上位机→模块，仅单播） */
#define CAN_DBG_CMD_WR_DATA   0x33U /* 写数据（上位机→模块，单帧） */
#define CAN_DBG_CMD_WR_REPLY  0x34U /* 写应答（模块→上位机，写方向唯一终态帧） */
#define CAN_DBG_CMD_CRC_VERIFY 0x35U /* CRC 校验（单帧请求单帧应答，无事务状态） */

/* 0x34 写应答状态码 */
typedef enum {
    CAN_DBG_WR_OK = 0,        /* 写入成功 */
    CAN_DBG_WR_ERR_FLASH,     /* 目标区落在 Flash，拒绝 */
    CAN_DBG_WR_ERR_WORDS,     /* 字数非法（含 0x33 DLC 与登记字数×2 不符） */
    CAN_DBG_WR_ERR_TIMEOUT,   /* 等待写数据超时 */
} CanDbgWr_Err_t;

/* 0x35 CRC 校验应答状态码（B4） */
typedef enum {
    CAN_DBG_CRC_OK = 0,       /* 校验完成，B0-3 为 CRC32 */
    CAN_DBG_CRC_ERR_RANGE,    /* 范围非法：字数 0/超上限，或越出 Flash 数组 */
} CanDbgCrc_Err_t;

/* 五段式帧 ID 组装（err=0、dev=CAN_DEV_OWN，与 can_app.c 同一布局） */
static inline uint32_t can_dbg_id_make(uint16_t cmd, uint16_t dest, uint16_t src)
{
    return (((uint32_t)CAN_DEV_OWN & 0x0FU) << 22) |
           (((uint32_t)cmd & 0x3FU) << 16) |
           (((uint32_t)dest & 0xFFU) << 8) |
           ((uint32_t)src & 0xFFU);
}

#if CAN_DBG_ENABLE
#include "can_dbg_rd.h"
#include "can_dbg_wr.h"
#include "can_dbg_crc.h"
#else
/* 调试口关闭：接口内联为空实现 */
static inline void can_dbg_rd_start(const HDL_CAN_Frame_t *f) { (void)f; }
static inline void can_dbg_rd_periodic(void) {}
static inline void can_dbg_rd_abort(void) {}
static inline void can_dbg_wr_start(const HDL_CAN_Frame_t *f) { (void)f; }
static inline void can_dbg_wr_data(const HDL_CAN_Frame_t *f) { (void)f; }
static inline void can_dbg_wr_periodic(void) {}
static inline void can_dbg_wr_abort(void) {}
static inline void can_dbg_crc_start(const HDL_CAN_Frame_t *f) { (void)f; }
#endif

#ifdef __cplusplus
}
#endif
#endif /* __CAN_DBG_H */
