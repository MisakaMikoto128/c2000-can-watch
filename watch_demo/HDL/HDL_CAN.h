/* HDL_CAN.h - CAN 帧收发。
 *
 * 这里的函数都不阻塞，也只能在主循环里调用，不要放进中断。
 * Write 系列只把帧排进发送队列，真正交给硬件邮箱由 HDL_CAN_Poll() 完成。
 * 收不下的帧直接丢掉，不返回错误码；每种丢弃各记一个数，用下面的 Get*Count() 查。
 */
#ifndef HDL_CAN_H_
#define HDL_CAN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* 一帧最多带 8 个数据字节 */
#define HDL_CAN_DATA_LEN   8u

/** @brief 一帧 CAN：ID 是标准帧的 11 位或扩展帧的 29 位，数据 0..8 个字节。
 *         C28x 的一个字是 16 位，所以 data[] 每个元素只用低 8 位存一个字节；
 *         数据不足 8 个时后面的元素填 0。 */
typedef struct {
    uint32_t id;                       // 标准帧或扩展帧的 ID 值
    uint16_t xtd;                      // 1 = 扩展帧，0 = 标准帧
    uint16_t dlc;                      // 这一帧有几个数据字节（0..8）
    uint16_t data[HDL_CAN_DATA_LEN];
} HDL_CAN_Frame_t;

/** @brief CAN 波特率档位，HDL_CAN_Init() 的入参。
 *  @note  档位与位时序寄存器值的对应关系集中在 HDL_CAN.c 的常量表里，
 *         新增档位只需在表里加一行。 */
typedef enum {
    CAN_BAUD_125K  = 0,    /* 125 kbps，出厂默认 */
    CAN_BAUD_250K  = 1,
    CAN_BAUD_500K  = 2,
    CAN_BAUD_1000K = 3,
    CAN_BAUD_NUM   = 4     /* 档位个数，只作表长用 */
} HDL_CAN_Baud_t;

/** @brief 按 baud 指定的波特率初始化并进入可收发状态，收发队列清空，丢帧计数保留。
 *  @note  会阻塞毫秒量级，必须第一个调用；再调一次就是重新初始化。
 *         传非法档位按 125k 兜底。 */
void HDL_CAN_Init(HDL_CAN_Baud_t baud);

/** @brief 收发各推进一次：新到的帧移进接收队列，发送队列里的帧交给硬件。
 *  @note  两次调用之间最多接得住 16 帧（硬件接收邮箱的个数），再多就有帧被后到的
 *         顶掉，被顶掉的次数由 HDL_CAN_GetRxMsgLstCount() 读出。 */
void HDL_CAN_Poll(void);

/** @brief 把一帧扩展帧排进发送队列；队列满时这一帧被丢弃。 */
void HDL_CAN_WriteExt(uint32_t id, const uint16_t *data, uint16_t dlc);

/** @brief 把一帧排进发送队列，标准帧还是扩展帧看 frame->xtd；frame 传空就直接返回。
 *  @note  队列满时这一帧被丢弃。 */
void HDL_CAN_WriteFrame(const HDL_CAN_Frame_t *frame);

/** @brief 从接收队列取一帧，取到返回 true；队列空或 frame 传空返回 false。
 *  @note  标准帧取出来的 id 是 11 位那一段的值。 */
bool HDL_CAN_Read(HDL_CAN_Frame_t *frame);

/** @brief 只让满足 (id & mask) == (filter_id & mask) 的帧进接收队列，设完立刻生效。 */
void HDL_CAN_SetRxFilter(uint32_t filter_id, uint32_t mask);

/** @brief 取消接收过滤，恢复全收。 */
void HDL_CAN_ClearRxFilter(void);

/** @brief 从 Init 到现在，接收队列因为满而丢掉的帧数 */
uint32_t HDL_CAN_GetRxQueueDropCount(void);
/** @brief 从 Init 到现在，发送队列因为满而丢掉的帧数 */
uint32_t HDL_CAN_GetTxQueueDropCount(void);
/** @brief 从 Init 到现在，硬件接收邮箱被后到的帧顶掉的次数 */
uint32_t HDL_CAN_GetRxMsgLstCount(void);
/** @brief 从 Init 到现在，因为硬件不响应而没能发出去的帧数 */
uint32_t HDL_CAN_GetTxSubmitFailCount(void);
/** @brief 发送软件队列当前空位数（0~8）。调用方在入队前查它，可以在
 *         "塞不进就不塞"与"塞进去被丢"之间选前者。 */
uint16_t HDL_CAN_TxQueueFree(void);
/** @brief 硬件发送邮箱是否全部空闲（没有任何在途帧）。同 ID 多帧流式发送
 *         必须等它为真再排下一帧：多帧并存多个邮箱时，DCAN 的发送顺序由
 *         邮箱号决定而不是入队顺序，帧序会被打乱。 */
bool HDL_CAN_TxAllIdle(void);

#ifdef __cplusplus
}
#endif

#endif // HDL_CAN_H_
