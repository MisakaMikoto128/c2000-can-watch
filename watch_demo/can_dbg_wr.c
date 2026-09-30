/* can_dbg_wr.c - 调试内存写事务：0x32 受理校验 → 等待 0x33 → 关中断写入 → 0x34 终态。
 *
 * 写入必须关中断：写与读不同，写错就是改错运行中的控制状态，不存在
 * "错一点没关系"的场景。≤4 字的写入是两条 32 位指令量级，关中断 <0.1 us。
 * Flash 区拒绝依据 HDL_Flash 的地址区间：App 运行时写 Flash 需要擦除与
 * 关中断毫秒级冻结，且误写会破坏在跑的固件镜像。写外设寄存器区、写通信
 * 栈自身状态均放行，后果由调用方承担。
 */
#include "can_dbg_wr.h"
#include "can_dbg.h" /* CAN_DBG_ENABLE 开关来源 */

#if CAN_DBG_ENABLE

#include "HDL_Flash.h"
#include "HDL_CPU_Time.h"
#include "CPU_Define.h"      /* DISABLE_INT/ENABLE_INT */
/* 写事务状态：受理进入 WAIT_DATA，落地/拒绝/超时都回到 IDLE */
typedef enum {
    CAN_DBG_WR_IDLE = 0U,
    CAN_DBG_WR_WAIT_DATA,
} CanDbgWr_State_t;

typedef struct {
    CanDbgWr_State_t state;
    byte_t   seq;         /* 本事务序号，0x34 B0 回显 */
    uint32_t addr;        /* 目标起始地址（C 字地址） */
    uint16_t words;       /* 写入字数 1~4，0x34 的长度回显 = words×2 */
    uint16_t timeout_ms;  /* 等待写数据的超时（0x32 B5） */
    uint32_t wait_start_ms; /* 进入写等待的时刻，periodic 按它判超时 */
} CanDbgWr_T;

static CanDbgWr_T s_wr;

/* 0x34 写方向唯一终态回复：B0 序号回显、B1 状态码、B2-5 地址回显、B6-7 长度 */
static void wr_reply(CanDbgWr_Err_t err)
{
    uint16_t tx[8];
    const uint32_t addr = s_wr.addr;
    const uint16_t len = (uint16_t)(s_wr.words << 1);

    tx[0] = s_wr.seq;
    tx[1] = err;
    tx[2] = addr & 0xFFU;
    tx[3] = (addr >> 8) & 0xFFU;
    tx[4] = (addr >> 16) & 0xFFU;
    tx[5] = (addr >> 24) & 0xFFU;
    tx[6] = len & 0xFFU;
    tx[7] = (len >> 8) & 0xFFU;
    HDL_CAN_WriteExt(can_dbg_id_make(CAN_DBG_CMD_WR_REPLY, CAN_ADDR_HOST, CAN_NODE_ADDR), tx, 8U);
}

/* 事务范围 [addr, addr+words) 是否与 Flash 区间相交 */
static bool wr_hit_flash(uint32_t addr, uint16_t words)
{
    return (addr <= HDL_FLASH_END_ADDRESS) &&
           ((addr + words) > HDL_FLASH_START_ADDRESS);
}

void can_dbg_wr_abort(void)
{
    s_wr.state = CAN_DBG_WR_IDLE;
}

void can_dbg_wr_start(const HDL_CAN_Frame_t *f)
{
    const uint32_t addr = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) |
                          ((uint32_t)f->data[2] << 16) | ((uint32_t)f->data[3] << 24);
    const uint16_t words = f->data[4];

    /* 调用契约：调度层已保证 dest 为本机地址的单播 */
    if (f->dlc < 8U) {
        return;
    }

    can_dbg_wr_abort();
    s_wr.seq = f->data[6];
    s_wr.addr = addr;
    s_wr.words = words;

    if (words == 0U || words > CAN_DBG_WR_MAX_WORDS) {
        wr_reply(CAN_DBG_WR_ERR_WORDS);
        return;
    }
    if (wr_hit_flash(addr, words)) {
        wr_reply(CAN_DBG_WR_ERR_FLASH);
        return;
    }

    s_wr.timeout_ms = f->data[5];
    s_wr.wait_start_ms = HDL_CPU_Time_GetTick();
    s_wr.state = CAN_DBG_WR_WAIT_DATA;
}

void can_dbg_wr_data(const HDL_CAN_Frame_t *f)
{
    uint16_t i;

    /* 事务已终结后的迟到写数据帧静默丢弃（超时终态已回过 0x34） */
    if (s_wr.state != CAN_DBG_WR_WAIT_DATA) {
        return;
    }

    if (f->dlc != (uint16_t)(s_wr.words << 1)) {
        s_wr.state = CAN_DBG_WR_IDLE;
        wr_reply(CAN_DBG_WR_ERR_WORDS);
        return;
    }

    DISABLE_INT();
    for (i = 0U; i < s_wr.words; i++) {
        /* 帧字节与字的映射：低字节在前（第 2k 字节 = 字低 8 位） */
        ((volatile uint16_t *)s_wr.addr)[i] =
            (uint16_t)(f->data[i << 1] | ((uint16_t)f->data[(i << 1) + 1U] << 8));
    }
    ENABLE_INT();

    s_wr.state = CAN_DBG_WR_IDLE;
    wr_reply(CAN_DBG_WR_OK);
}

void can_dbg_wr_periodic(void)
{
    if (s_wr.state != CAN_DBG_WR_WAIT_DATA) {
        return;
    }
    if (HDL_CPU_Time_Elapsed(&s_wr.wait_start_ms, s_wr.timeout_ms)) {
        s_wr.state = CAN_DBG_WR_IDLE;
        wr_reply(CAN_DBG_WR_ERR_TIMEOUT);
    }
}

#endif /* CAN_DBG_ENABLE */
