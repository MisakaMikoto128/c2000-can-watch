/* can_dev.h - 自有协议设备段与本机地址的唯一来源。
 *
 * 固件（App + Bootloader）与上位机（protocol.py 的 OWN_DEV/NODE_ADDR）从同一组
 * 值出发：若要迁移 dev 段或本机地址，改这一处并重编译固件、同步上位机即可。
 * 约束：dev 必须在 0x0~0xF（帧 ID 的 4 位设备段）；设备地址 0x01~0x3B，
 * 0x3F 是广播、0xF0 是上位机，不得用作设备地址。
 */
#ifndef __CAN_DEV_H
#define __CAN_DEV_H

/* 自有协议统一设备段：App 命令 0x01~0x07、升级命令 0x20~0x27、
 * 调试内存读写 0x30~0x35 全部挂这里 */
#define CAN_DEV_OWN 0x0CU

/* 本节点 CAN 地址：演示固件为编译期常量，移植时可改为拨码或存储读取 */
#define CAN_NODE_ADDR 1U

/* 目的地址约定 */
#define CAN_ADDR_BROADCAST 0x3FU
#define CAN_ADDR_HOST      0xF0U

#endif /* __CAN_DEV_H */
