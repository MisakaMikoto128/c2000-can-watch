/* BFL_CRC.c - CRC32 实现。 */
#include "BFL_CRC.h"

#define BFL_CRC_POLY   0xEDB88320UL
#define BFL_CRC_SEED   0xFFFFFFFFUL
#define BFL_CRC_XOROUT 0xFFFFFFFFUL

#if defined(_FLASH) && defined(BFL_CRC_USE_HARDWARE)
#pragma RETAIN(BFL_CRC_SelfTest)
#endif

/* 软件实现两种后端下都要留着：没开硬件时它负责算，开了硬件时 SelfTest 拿它当对照 */
static uint32_t sw_update(uint32_t crc, const byte_t *bytes, uint32_t len)
{
    for (uint32_t i = 0U; i < len; i++) {
        crc ^= bytes[i];
        for (uint16_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^ (BFL_CRC_POLY & (0UL - (crc & 1UL)));
        }
    }
    return crc;
}

#ifdef BFL_CRC_USE_HARDWARE

/* C28x 的内联汇编写不了操作数，所以入参和结果都先放进下面这几个静态变量，
 * 由汇编通过 DP 相对寻址去读写 */
static const byte_t *s_msg;
static uint32_t s_seed;
static uint32_t s_result;

uint32_t BFL_CRC_Init(void)
{
    return BFL_CRC_SEED;
}

uint32_t BFL_CRC_Update(uint32_t crc, const byte_t *bytes, uint32_t len)
{
    /* VSETCRCMSGFLIP 加上固定多项式 0x04C11DB7 的 VCRC32L_1，走出来的正好是软件那套反射路径；
     * 硬件每次也只取元素的低 8 位，和 byte_t 的用法对得上 */
    s_msg = bytes;
    s_seed = crc;
    asm(" VSETCRCMSGFLIP ");
    asm(" MOV DP,#s_seed ");
    asm(" VMOV32 VCRC,@s_seed ");
    asm(" MOV DP,#s_msg ");
    asm(" MOVL XAR6,@s_msg ");
    for (uint32_t i = 0U; i < len; i++) {
        asm(" VCRC32L_1 *XAR6++ ");
    }
    asm(" MOV DP,#s_result ");
    asm(" VMOV32 @s_result,VCRC ");
    asm(" VCLRCRCMSGFLIP ");
    (void)s_msg; /* 内联汇编读取，编译器不可见 */
    (void)s_seed;
    return s_result;
}

/* 硬件里存的是软件结果逐位反过来的值：FLIP 把 32 位拆成的两个 16 位半字各自翻转，
 * 再把两个半字对调，就翻回软件那样的正常值 */
static uint32_t s_tmp;

static uint32_t bitrev32(uint32_t x)
{
    s_tmp = x;
    asm(" MOV DP,#s_tmp ");
    asm(" MOVL ACC,@s_tmp ");
    asm(" FLIP AL ");
    asm(" FLIP AH ");
    asm(" MOVL @s_tmp,ACC ");
    return (s_tmp << 16) | (s_tmp >> 16);
}

#else

uint32_t BFL_CRC_Init(void)
{
    return BFL_CRC_SEED;
}

uint32_t BFL_CRC_Update(uint32_t crc, const byte_t *bytes, uint32_t len)
{
    return sw_update(crc, bytes, len);
}

#endif

uint32_t BFL_CRC_Final(uint32_t crc)
{
#ifdef BFL_CRC_USE_HARDWARE
    return bitrev32(crc) ^ BFL_CRC_XOROUT;
#else
    return crc ^ BFL_CRC_XOROUT;
#endif
}

uint32_t BFL_CRC(const byte_t *bytes, uint32_t len)
{
    return BFL_CRC_Final(BFL_CRC_Update(BFL_CRC_Init(), bytes, len));
}

bool BFL_CRC_SelfTest(void)
{
    static const byte_t v1[9U] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    byte_t v2[256U];

    for (uint32_t i = 0U; i < 256U; i++) {
        v2[i] = (byte_t)((i * 37U + 11U) & 0xFFU);
    }
    /* "123456789" 的 CRC-32 标准答案就是 0xCBF43926，业界通用 */
    if (BFL_CRC(v1, 9U) != 0xCBF43926UL) {
        return false;
    }
    if (BFL_CRC(v2, 256U) != 0x8ED7A350UL) {
        return false;
    }
    if (BFL_CRC(v2, 256U) != (sw_update(BFL_CRC_SEED, v2, 256U) ^ BFL_CRC_XOROUT)) {
        return false;
    }
    return true;
}
