/* app_config.h - 演示固件配置开关。 */
#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* 调试内存读写命令组（0x30~0x35）：1 = 编译进固件，0 = 各接口在本文件内联为空实现 */
#define CAN_DBG_ENABLE 1

#endif /* __APP_CONFIG_H */
