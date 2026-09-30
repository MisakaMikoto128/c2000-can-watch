/* HDL_Flash.h - Flash 擦写 HDL 层。
 *
 * 全部长度/偏移以 16 位字计（本机最小可寻址单元），缓冲为 uint16_t 数组。
 * NOTE: 擦写期间 Flash 不响应取指（毫秒级），调用期间不得有取指自 Flash 的
 * 代码在运行（含中断）。
 */
#ifndef HDL_FLASH_H
#define HDL_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* 单扇区字数 */
#define HDL_FLASH_LOGIC_SECTOR_SIZE        4096U
#define HDL_FLASH_SECTOR_NB                16U
/* 16 位字地址 */
#define HDL_FLASH_START_ADDRESS            0x00080000UL
#define HDL_FLASH_END_ADDRESS              (HDL_FLASH_START_ADDRESS + \
                                            (uint32_t)HDL_FLASH_LOGIC_SECTOR_SIZE * HDL_FLASH_SECTOR_NB - 1U)
#define HDL_FLASH_LOGIC_SECTOR_OF_FLASH_ADDRESS(flash_address) \
                                            (((flash_address) - HDL_FLASH_START_ADDRESS) / HDL_FLASH_LOGIC_SECTOR_SIZE)

/**
 * @brief 初始化：配置取指等待周期/prefetch/cache + 擦写时序。
 * @note  这一步按当前的系统时钟和等待周期算出擦写时序，并激活要用到的 Flash bank。
 *        没跑过它，所有擦写都会失败；改了主频或等待周期就得重新跑一次。
 */
void HDL_Flash_Init(void);

/**
 * @brief 擦除若干逻辑扇区。
 * @param logic_sector_start 起始扇区号，0 ~ HDL_FLASH_SECTOR_NB-1
 * @param sector_nb 扇区数量，0 表示无需擦除直接返回 true
 * @return true 全部扇区擦除并通过空校验；false 参数越界或任一扇区失败
 */
bool HDL_Flash_Erase(uint32_t logic_sector_start, uint32_t sector_nb);

/**
 * @brief 擦除整个 Flash。
 */
void HDL_Flash_MassErase(void);

/**
 * @brief 写数据（先自动擦除覆盖的扇区，再编程）。
 * @param logic_sector_start 起始扇区号
 * @param max_sector_nb 允许写入的扇区数量上限
 * @param sector_word_offset 相对起始扇区首地址的字偏移，须 4 字对齐
 * @param buf 字数组
 * @param word_len 期望写入的字数，超出扇区容量部分被截断
 * @return uint32_t 实际写入的字数；参数非法或擦写/校验失败返回 0
 */
uint32_t HDL_Flash_Write(uint32_t logic_sector_start, uint32_t max_sector_nb,
                         uint32_t sector_word_offset, const uint16_t *buf, uint32_t word_len);

/**
 * @brief 写数据到单个扇区，不擦除。目标 4 字片必须是擦除态；已经编程过的片子再写，
 *        写回相同数据硬件允许，写不同数据会被硬件拒绝。
 * @return uint32_t 实际写入的字数；参数非法或编程/校验失败返回 0
 */
uint32_t HDL_Flash_Sector_Write_NoErase(uint32_t logic_sector_start,
                                        uint32_t sector_word_offset, const uint16_t *buf,
                                        uint32_t word_len);

/**
 * @brief 读数据（跨扇区连续读取，超出扇区容量部分被截断）。
 * @return uint32_t 实际读取的字数；参数非法返回 0
 */
uint32_t HDL_Flash_Read(uint32_t logic_sector_start, uint32_t max_sector_nb,
                        uint32_t sector_word_offset, uint16_t *buf, uint32_t word_len);

/**
 * @brief 整扇区写入（先擦后写）。
 * @return uint32_t 实际写入的字数（恒为扇区大小）；失败返回 0
 */
uint32_t HDL_Flash_SectorWrite(uint32_t logic_sector_start, const uint16_t *buf);

/**
 * @brief 整扇区读取。
 * @return uint32_t 实际读取的字数（恒为扇区大小）；失败返回 0
 */
uint32_t HDL_Flash_SectorRead(uint32_t logic_sector_start, uint16_t *buf);

/**
 * @brief 从指定字地址起读数据。
 * @return uint32_t 实际读取的字数；地址越界返回 0
 */
uint32_t HDL_Flash_Read_From(uint32_t start_address, uint16_t *buf, uint32_t word_len);

/**
 * @brief 逻辑扇区号 → 扇区首地址（字地址）。
 */
uint32_t HDL_Flash_GetSectorStartAddress(uint32_t logic_sector);

#ifdef __cplusplus
}
#endif
#endif /* HDL_FLASH_H */
