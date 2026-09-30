/**
 * @file cqueue.c
 * @author Liu Yuanlin
 * @brief
 * @version 0.1
 * @date 2023-09-13
 * @last modified 2023-09-13
 *
 * @copyright Copyright (c) 2023 Liu Yuanlin (CC BY-NC-SA 4.0).
 *
 */
#include "cqueue.h"

/**
 * @brief 创建一个静态的循环数组队列。
 *
 * @param Q 队列对象的指针。
 * @param pBuf 队列数据存放内存指针，容量应当为
 * @param uiBufElementCapacity 缓存能够存放的最大元素个数，但是队列实际能够存放的元素个数为uiBufElementCapacity-1。
 * @param uiItemSize 队列中每个元素的大小。
 * @return byte_t 1成功，0失败。
 */
byte_t cqueue_create(CQueue Q, CObject_t pBuf, uint32_t uiBufElementCapacity, uint32_t uiItemSize)
{
    if (uiBufElementCapacity == 0 || uiItemSize == 0) {
        return 0;
    }

    Q->CapacityOfElement = uiBufElementCapacity;
    Q->pData             = pBuf;
    Q->ItemSize          = uiItemSize;
    Q->QueueCapacity     = uiBufElementCapacity - 1;

    // Make empty
    cqueue_make_empty(Q);
    return 0;
}

/**
 * @brief 队列入队一个元素。
 *
 * @param Q
 * @param obj 指向入队元素对象的指针。
 * @return byte_t 1入队成功，0入队失败队列满了。
 */
byte_t cqueue_enqueue(CQueue Q, const CObject_t obj)
{
    byte_t ret = 0;
    if (!cqueue_is_full(Q)) {
        byte_t *pDst = (byte_t *)(Q->pData) + Q->Rear * Q->ItemSize;
        byte_t *pSrc = (byte_t *)obj;
        memcpy(pDst, pSrc, Q->ItemSize);
        Q->Rear = (Q->Rear + 1) % Q->CapacityOfElement;
        ret     = 1;
    }
    return ret;
}

/**
 * @brief 队列出队一个元素。
 *
 * @param Q
 * @param obj 指向出队元素对象的指针。
 * @return byte_t 1出队成功，0出队失败队列为空。
 */
byte_t cqueue_dequeue(CQueue Q, CObject_t obj)
{
    byte_t ret = 0;
    if (!cqueue_is_empty(Q)) {
        byte_t *pDst = (byte_t *)obj;
        byte_t *pSrc = (byte_t *)(Q->pData) + Q->Front * Q->ItemSize;
        // font
        if (obj != NULL) {
            memcpy(pDst, pSrc, Q->ItemSize);
        }
        // dequeue
        Q->Front = (Q->Front + 1) % Q->CapacityOfElement;
        ret      = 1;
    }

    return ret;
}

/**
 * @brief 返回队列头部元素。
 *
 * @param Q
 * @param pX 指向出队元素对象的指针。
 * @return byte_t 1 成功，0失败，队列为空。
 */
byte_t cqueue_peek(CQueue Q, CObject_t pX)
{
    byte_t ret = 0;
    if (!cqueue_is_empty(Q)) {
        byte_t *pDst = (byte_t *)pX;
        byte_t *pSrc = (byte_t *)(Q->pData) + Q->Front * Q->ItemSize;
        // font
        memcpy(pDst, pSrc, Q->ItemSize);
        ret = 1;
    }
    return ret;
}

/**
 * @brief 向队列中入队一段数据。
 *
 * @param Q
 * @param pBuf 指向入队元素内存的指针。
 * @param bufSize 入队元素的个数。
 * @return uint32_t 实际入队的元素个数。
 */
uint32_t cqueue_in(CQueue Q, const CObject_t pBuf, uint32_t bufSize)
{
    uint32_t ret      = 0;
    byte_t *pBufTemp = pBuf;
    while (!cqueue_is_full(Q) && bufSize > 0) {
        byte_t *pDst = (byte_t *)(Q->pData) + Q->Rear * Q->ItemSize;
        byte_t *pSrc = (byte_t *)pBufTemp;
        memcpy(pDst, pSrc, Q->ItemSize);
        pBufTemp += Q->ItemSize;
        // enqueue
        Q->Rear = (Q->Rear + 1) % Q->CapacityOfElement;
        bufSize--;
        ret++;
    }
    return ret;
}

/**
 * @brief 将队列中一部分元素出队
 *
 * @param Q
 * @param pBuf 指向出存放出队元素内存的指针。
 * @param bufSize 希望出队的元素个数。
 * @return uint32_t 实际出队的元素个数。
 */
uint32_t cqueue_out(CQueue Q, CObject_t pBuf, uint32_t bufSize)
{
    uint32_t ret = 0;

    byte_t *pBufTemp = pBuf;
    while (!cqueue_is_empty(Q) && bufSize > 0) {
        byte_t *pDst = (byte_t *)pBufTemp;
        byte_t *pSrc = (byte_t *)(Q->pData) + Q->Front * Q->ItemSize;
        // font
        memcpy(pDst, pSrc, Q->ItemSize);
        pBufTemp = pBufTemp + Q->ItemSize;
        // dequeue
        Q->Front = (Q->Front + 1) % Q->CapacityOfElement;
        bufSize--;
        ret++;
    }

    return ret;
}
