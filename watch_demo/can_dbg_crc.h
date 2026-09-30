/**
* @file can_dbg_crc.h
* @brief 调试 CRC 校验命令：0x35 单帧请求 → 固件同步算 CRC32 → 单帧应答。
*
* 用途：上位机连接校验一次请求拿到板上固件区 CRC32，与 .out 镜像 CRC 比对，
* 替代 0x30 逐段读回（整区逐段读秒级，0x35 毫秒级）。无事务状态：不占用
* 读/写单槽，不打断在途 0x30/0x32 事务，应答帧 ID 与 0x31 流互不干扰。
*/
#ifndef __CAN_DBG_CRC_H
#define __CAN_DBG_CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "HDL_CAN.h"

/* 单次校验字数上限（0x35 B4-5）：演示固件独立运行，覆盖整个 Flash
 * （16 扇段 × 4096 字）；若 App 与 Bootloader 共存，按实际 App 区大小收紧 */
#define CAN_DBG_CRC_MAX_WORDS 65536U

/** @brief 受理一帧 CRC 校验（cmd 0x35）。同步算完即回，无事务状态。 */
void can_dbg_crc_start(const HDL_CAN_Frame_t *f);

#ifdef __cplusplus
}
#endif
#endif /* __CAN_DBG_CRC_H */
