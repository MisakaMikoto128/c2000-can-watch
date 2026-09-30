/* can_dbg_crc.c - 调试 CRC 校验：0x35 单帧请求 → 固件算 CRC32 → 单帧应答。
 *
 * CRC 口径与 BL 升级链路 VERIFY 完全一致（zlib.crc32）：BFL_CRC_Init/Update/
 * Final + 字序适配。计算同步在收帧路径完成：Flash 内容静态（普通指针读，
 * 非 volatile），整区毫秒级，主循环阻塞一次可接受。校验范围限定 Flash 数组——对 RAM 算 CRC 无意义（内容随运行
 * 变化），对未实现区域读数无效。
 */
#include "can_dbg_crc.h"
#include "can_dbg.h" /* CAN_DBG_ENABLE 开关来源、CAN_DBG_CMD_*、帧 ID 组装 */

#if CAN_DBG_ENABLE

#include "BFL_CRC.h"
#include "HDL_Flash.h"       /* HDL_FLASH_START/END_ADDRESS 校验范围 */

/* 上位机按 8 位字节流算 CRC，所以每个 16 位字要先拆成两个字节、低字节在前
 * （原样取自 boot/boot_protocol.c 的 crc32_update_words） */
static uint32_t crc32_update_words(uint32_t crc, const uint16_t *words, uint32_t word_nb)
{
    for (uint32_t i = 0U; i < word_nb; i++) {
        byte_t b[2U];

        b[0] = (byte_t)(words[i] & 0xFFU);
        b[1] = (byte_t)(words[i] >> 8);
        crc = BFL_CRC_Update(crc, b, 2U);
    }
    return crc;
}

void can_dbg_crc_start(const HDL_CAN_Frame_t *f)
{
    uint16_t tx[8];
    const uint32_t addr = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) |
                          ((uint32_t)f->data[2] << 16) | ((uint32_t)f->data[3] << 24);
    const uint16_t words = (uint16_t)(f->data[4] | ((uint16_t)f->data[5] << 8));
    const uint16_t *src = (const uint16_t *)addr;
    uint32_t crc;

    /* 调用契约：调度层已保证 dest 为本机地址的单播 */
    if (f->dlc < 8U) {
        return;
    }
    /* 范围非法：字数 0/超上限，或区间越出 Flash 数组（words-1 与 END-addr
     * 比较同时覆盖 32 位回绕） */
    if ((words == 0U) || (words > CAN_DBG_CRC_MAX_WORDS) ||
        (addr < HDL_FLASH_START_ADDRESS) ||
        ((uint32_t)(words - 1U) > (uint32_t)(HDL_FLASH_END_ADDRESS - addr))) {
        tx[0] = 0U;
        tx[1] = 0U;
        tx[2] = 0U;
        tx[3] = 0U;
        tx[4] = CAN_DBG_CRC_ERR_RANGE;
        tx[5] = 0U;
        tx[6] = 0U;
        tx[7] = 0U;
        HDL_CAN_WriteExt(can_dbg_id_make(CAN_DBG_CMD_CRC_VERIFY, CAN_ADDR_HOST, CAN_NODE_ADDR), tx, 8U);
        return;
    }

    crc = BFL_CRC_Final(crc32_update_words(BFL_CRC_Init(), src, words));

    tx[0] = crc & 0xFFU;
    tx[1] = (crc >> 8) & 0xFFU;
    tx[2] = (crc >> 16) & 0xFFU;
    tx[3] = (crc >> 24) & 0xFFU;
    tx[4] = CAN_DBG_CRC_OK;
    tx[5] = 0U;
    tx[6] = 0U;
    tx[7] = 0U;
    HDL_CAN_WriteExt(can_dbg_id_make(CAN_DBG_CMD_CRC_VERIFY, CAN_ADDR_HOST, CAN_NODE_ADDR), tx, 8U);
}

#endif /* CAN_DBG_ENABLE */
