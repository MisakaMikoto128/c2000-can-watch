/* can_dbg_rd.c - 调试内存读事务：0x30 受理（快照/直读）→ 0x31 流式发送。
 *
 * 快照模式（B7=0）受理时关中断把目标内存整块拷入快照缓冲，上位机拿到的
 * 是同一时刻的原子快照：255 字拷贝约 2.5~3 us，期间到点的中断只延迟不丢失。
 * 直读模式（B7≠0）不快照不关中断，发送时逐帧读目标地址当前值，各字节可能
 * 来自不同时刻（撕裂），只供观察，由调用方自行判断数据可信度。
 * 0x31 帧 i 的 B1-7 覆盖全局字节偏移 [7i, 7i+7)：第 k 字节取字 k/2 的
 * 低 8 位（k 偶）或高 8 位（k 奇），末帧不足补 0。gap_ms=0 时按线速连发，
 * 发送软件队列满则跳过本轮，不丢帧。
 */
#include "can_dbg_rd.h"
#include "can_dbg.h" /* CAN_DBG_ENABLE 开关来源 */
#include "HDL_CPU_Time.h"
#include "CPU_Define.h"      /* DISABLE_INT/ENABLE_INT */

#if CAN_DBG_ENABLE

/* 读事务状态：受理即进入 RUN，发满 total_bytes 回到 IDLE */
typedef enum {
    CAN_DBG_RD_IDLE = 0U,
    CAN_DBG_RD_RUN,
} CanDbgRd_State_t;

typedef struct {
    CanDbgRd_State_t state;
    byte_t   seq;         /* 本事务序号，0x31 B0 回显 */
    uint32_t addr;        /* 目标起始地址（C 字地址） */
    uint16_t total_bytes; /* 数据总字节数 = 字数×2 */
    uint16_t sent_bytes;  /* 已发送字节数（全局切片游标） */
    uint16_t gap_ms;      /* 帧间隔 ms，0 = 线速连发 */
    uint32_t next_ms;     /* 下一帧允许发送的时刻（gap_ms>0 时检查） */
    bool     direct;      /* true = 直读（不快照不关中断） */
    uint16_t snapshot[CAN_DBG_RD_MAX_WORDS]; /* 快照缓冲（直读模式不用） */
} CanDbgRd_T;

static CanDbgRd_T s_rd;

void can_dbg_rd_abort(void)
{
    s_rd.state = CAN_DBG_RD_IDLE;
}

/* 取全局字节偏移 off 处的一个字节：字 off/2 的低 8 位（off 偶）或高 8 位（off 奇） */
static byte_t rd_byte_at(uint16_t off)
{
    const volatile uint16_t *src = (const volatile uint16_t *)s_rd.addr;
    const uint16_t w = s_rd.direct ? src[off >> 1] : s_rd.snapshot[off >> 1];

    return ((off & 1U) != 0U) ? (byte_t)(w >> 8) : (byte_t)(w & 0xFFU);
}

void can_dbg_rd_start(const HDL_CAN_Frame_t *f)
{
    const uint32_t addr = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) |
                          ((uint32_t)f->data[2] << 16) | ((uint32_t)f->data[3] << 24);
    const uint16_t words = f->data[4];

    /* 调用契约：调度层已保证 dest 为本机地址的单播 */
    if (f->dlc < 8U) {
        return;
    }
    /* 长度超出契约属帧格式错误，按既有协议惯例整帧忽略 */
    if (words == 0U || words > CAN_DBG_RD_MAX_WORDS) {
        return;
    }

    can_dbg_rd_abort();
    s_rd.addr = addr;
    s_rd.total_bytes = (uint16_t)(words << 1);
    s_rd.sent_bytes = 0U;
    s_rd.gap_ms = f->data[5];
    s_rd.seq = f->data[6];
    s_rd.direct = (f->data[7] != 0U);

    if (!s_rd.direct) {
        const volatile uint16_t *src = (const volatile uint16_t *)addr;
        uint16_t i;

        DISABLE_INT();
        for (i = 0U; i < words; i++) {
            s_rd.snapshot[i] = src[i];
        }
        ENABLE_INT();
    }

    s_rd.next_ms = HDL_CPU_Time_GetTick();
    s_rd.state = CAN_DBG_RD_RUN;
}

void can_dbg_rd_periodic(void)
{
    uint16_t tx[8];
    uint16_t n;
    uint16_t j;

    if (s_rd.state != CAN_DBG_RD_RUN) {
        return;
    }
    if (s_rd.gap_ms != 0U && !HDL_CPU_Time_Elapsed(&s_rd.next_ms, s_rd.gap_ms)) {
        return;
    }
    /* 节流：发送队列满不塞（帧不丢）；在途帧未清零也不塞——同 ID 多帧并存
     * 多个 TX 邮箱时发送顺序由邮箱号决定，任意时刻最多一帧在途才保帧序。
     * 主循环快于帧线上时间，该约束不损失带宽 */
    if (HDL_CAN_TxQueueFree() == 0U || !HDL_CAN_TxAllIdle()) {
        return;
    }

    n = (uint16_t)(s_rd.total_bytes - s_rd.sent_bytes);
    if (n > 7U) {
        n = 7U;
    }

    tx[0] = s_rd.seq;
    for (j = 0U; j < 7U; j++) {
        tx[j + 1U] = (j < n) ? rd_byte_at((uint16_t)(s_rd.sent_bytes + j)) : 0U;
    }
    HDL_CAN_WriteExt(can_dbg_id_make(CAN_DBG_CMD_RD_DATA, CAN_ADDR_HOST, CAN_NODE_ADDR), tx, 8U);

    s_rd.sent_bytes += n;
    if (s_rd.sent_bytes >= s_rd.total_bytes) {
        s_rd.state = CAN_DBG_RD_IDLE;
    }
}

#endif /* CAN_DBG_ENABLE */
