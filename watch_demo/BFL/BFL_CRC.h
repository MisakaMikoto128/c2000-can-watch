/* BFL_CRC.h - CRC32，结果与 zlib 一致：多项式反射，初值和终值都异或 0xFFFFFFFF。
 *
 * 后端靠编译宏 BFL_CRC_USE_HARDWARE 切换：不定义就用软件逐位，定义成 1 就用 VCRC
 * 硬件，这时编译器还得加 --vcu_support=vcrc。
 * 输入是一串 8 位字节，用 byte_t 承载，每个元素只有低 8 位有效。
 */
#ifndef BFL_CRC_H_
#define BFL_CRC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ccommon.h"

uint32_t BFL_CRC_Init(void);
uint32_t BFL_CRC_Update(uint32_t crc, const byte_t *bytes, uint32_t len);
uint32_t BFL_CRC_Final(uint32_t crc);
uint32_t BFL_CRC(const byte_t *bytes, uint32_t len);
/* 拿有标准答案的向量自算一遍，再跟软件结果比一次；开着硬件后端时用它确认 VCRC 没算错 */
bool BFL_CRC_SelfTest(void);

#ifdef __cplusplus
}
#endif
#endif /* BFL_CRC_H_ */
