/**
 * @file cqueue.h
 * @author Liu Yuanlin
 * @brief
 * @version 0.1
 * @date 2023-09-13
 * @last modified 2023-09-13
 *
 * @copyright Copyright (c) 2023 Liu Yuanlin (CC BY-NC-SA 4.0).
 *
 */
#ifndef CQUEUE_H
#define CQUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "cobject.h"
#include "ccommon.h"
/*
--> rear | | | | | | | font-->
*/

typedef struct
{
    uint32_t CapacityOfElement; // 队列缓存能够存放的元素个数，为@QueueCapacity+1
    uint32_t QueueCapacity;     // 队列能够存放的元素个数
    uint32_t Front;
    uint32_t Rear;
    uint32_t ItemSize;
    CObject_t *pData;
} CQueue_t;

typedef CQueue_t *CQueue;

byte_t cqueue_create(CQueue Q, CObject_t pBuf, uint32_t uiBufElementCapacity, uint32_t uiItemSize);
byte_t cqueue_enqueue(CQueue Q, const CObject_t pX);
byte_t cqueue_dequeue(CQueue Q, CObject_t pX);
byte_t cqueue_peek(CQueue Q, CObject_t pX);
uint32_t cqueue_in(CQueue Q, const CObject_t pBuf, uint32_t bufSize);
uint32_t cqueue_out(CQueue Q, CObject_t pBuf, uint32_t bufSize);

// 返回队列容量：元素个数
#define cqueue_capacity(Q) ((Q)->QueueCapacity)
/**
 * @brief 队列中元素个数。
 *
 * @param Q
 * @return uint32_t 队列中数据的大小。
 */
#define cqueue_size(Q) (((Q)->Rear - (Q)->Front + (Q)->CapacityOfElement) % (Q)->CapacityOfElement)
// 返回队列剩余容量：元素个数
#define cqueue_residual_capacity(Q) ((Q)->QueueCapacity - cqueue_size(Q))
// 判断队列是否为空，true 为空，false 非空
#define cqueue_is_empty(Q) ((Q)->Front == (Q)->Rear)
/**
 * @brief 队列是否满了。
 *
 * @param Q
 * @return byte_t 1为满，0不满。
 */
#define cqueue_is_full(Q) (((Q)->Rear + 1) % (Q)->CapacityOfElement == (Q)->Front)

/**
 * @brief 清空队列。
 *
 * @param Q
 */
#define cqueue_make_empty(Q) ((Q)->Front = (Q)->Rear = 0)
#ifdef __cplusplus
}
#endif
#endif //! CQUEUE_H
