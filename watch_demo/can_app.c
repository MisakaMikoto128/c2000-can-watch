/* can_app.c - 应用侧自有协议命令：0x20 状态查询、0x07 编译时刻，以及
 * 0x30~0x35 调试内存读写命令组的分发（实现见 can_dbg_rd/wr/crc）。
 *
 * 帧 ID 五段式：ID[28:0] = err[28:26] | dev[25:22] | cmd[21:16] | dest[15:8] | src[7:0]。
 * 下行命令寻址在 ID 的 dest 段，payload 不再带地址；调试命令只收单播——
 * 广播调试帧整帧忽略，不打断在途事务。
 * 其余 cmd 号收到即丢掉不回。
 */
#include "can_app.h"
#include "HDL_CPU_Time.h"
#include "app_version.h"
#include "can_dbg.h"

/* cmd 号（dev 0x0C 段内） */
#define CMD_PROBE      0x20U /* 状态查询（App/Bootloader 都应答，B1 区分侧） */
#define CMD_BUILD_TIME 0x07U /* 查询固件编译时刻，应答同 cmd 号 */

static uint32_t own_id_make(uint16_t cmd, uint16_t dest, uint16_t src)
{
    return (((uint32_t)CAN_DEV_OWN & 0x0FU) << 22) |
           (((uint32_t)cmd & 0x3FU) << 16) |
           (((uint32_t)dest & 0xFFU) << 8) |
           ((uint32_t)src & 0xFFU);
}

static uint16_t own_cmd_of(uint32_t id)  { return (uint16_t)((id >> 16) & 0x3FU); }
static uint16_t own_dest_of(uint32_t id) { return (uint16_t)((id >> 8) & 0xFFU); }

/* 0x20 状态查询应答：B0=0x20（cmd 回显）、B1=0x01 App、B2=本机地址、B3-5=版本 */
static void send_probe(void)
{
    uint16_t tx[8];

    tx[0] = CMD_PROBE;
    tx[1] = 0x01U; /* App */
    tx[2] = CAN_NODE_ADDR;
    tx[3] = APP_VER_MAJ;
    tx[4] = APP_VER_MIN;
    tx[5] = APP_VER_PATCH;
#if CAN_DBG_ENABLE
    tx[6] = 0x01U; /* B6 bit0 = 调试内存读写命令组已编译 */
#else
    tx[6] = 0U;
#endif
    tx[7] = 0U;
    HDL_CAN_WriteExt(own_id_make(CMD_PROBE, CAN_ADDR_HOST, CAN_NODE_ADDR), tx, 8U);
}

/* 固件编译时刻的 Unix 秒（__DATE__/__TIME__ 按编译器本地时钟、作 UTC 折算）。
 * C28x 的 int 是 16 位，时间部分先提升到 32 位再乘，避免溢出 */
static uint32_t build_time_unix(void)
{
    static const char *const months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static uint32_t cached = 0U;

    if (cached == 0U) {
        int m;
        for (m = 0; m < 12; m++) {
            if (__DATE__[0] == months[m][0] && __DATE__[1] == months[m][1] &&
                __DATE__[2] == months[m][2]) {
                break;
            }
        }
        const int month = m + 1;
        const int day = ((__DATE__[4] == ' ') ? 0 : (__DATE__[4] - '0')) * 10 + (__DATE__[5] - '0');
        const int year = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 +
                         (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
        const int hour = (__TIME__[0] - '0') * 10 + (__TIME__[1] - '0');
        const int minute = (__TIME__[3] - '0') * 10 + (__TIME__[4] - '0');
        const int sec = (__TIME__[6] - '0') * 10 + (__TIME__[7] - '0');
        /* days_from_civil：公历日期折成自 1970-01-01 起的天数 */
        const long long y = year - (month <= 2);
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned long long yoe = (unsigned long long)(y - era * 400);
        const unsigned long long doy =
            (153ULL * (unsigned long long)(month + (month > 2 ? -3 : 9)) + 2ULL) / 5ULL +
            (unsigned long long)day - 1ULL;
        const unsigned long long doe = yoe * 365ULL + yoe / 4ULL - yoe / 100ULL + doy;
        const long long days = era * 146097LL + (long long)doe - 719468LL;
        const uint32_t tod = (uint32_t)hour * 3600U + (uint32_t)minute * 60U +
                             (uint32_t)sec;
        cached = (uint32_t)((unsigned long long)days * 86400ULL + tod);
    }
    return cached;
}

/* 编译时刻应答：B0=0x07（cmd 回显）、B1=0x01 App、B2-5=Unix 秒（小端） */
static void send_build_time(void)
{
    uint16_t tx[8];
    const uint32_t ts = build_time_unix();

    tx[0] = CMD_BUILD_TIME;
    tx[1] = 0x01U;
    tx[2] = ts & 0xFFU;
    tx[3] = (ts >> 8) & 0xFFU;
    tx[4] = (ts >> 16) & 0xFFU;
    tx[5] = (ts >> 24) & 0xFFU;
    tx[6] = 0U;
    tx[7] = 0U;
    HDL_CAN_WriteExt(own_id_make(CMD_BUILD_TIME, CAN_ADDR_HOST, CAN_NODE_ADDR), tx, 8U);
}

void can_app_poll(void)
{
    HDL_CAN_Frame_t f;

    HDL_CAN_Poll();

    while (HDL_CAN_Read(&f)) {
        uint16_t cmd;
        uint16_t dest;

        if (f.xtd == 0u) {
            continue; /* 仅处理扩展帧 */
        }
        if (((f.id >> 22) & 0xFU) != CAN_DEV_OWN) {
            continue; /* 非自有协议设备段 */
        }
        cmd = own_cmd_of(f.id);
        dest = own_dest_of(f.id);
        /* 寻址在 ID 里：既不是广播也不是本机地址就整帧丢掉 */
        if (dest != CAN_ADDR_BROADCAST && dest != CAN_NODE_ADDR) {
            continue;
        }

        switch (cmd) {

        case CMD_PROBE:
            send_probe();
            break;

        case CMD_BUILD_TIME:
            send_build_time();
            break;

        /* 调试内存读写（0x30~0x35），仅单播。单槽互斥在受理入口完成：
         * 读开始作废在途写、写开始作废在途读，残留帧由上位机按序号过滤 */
        case CAN_DBG_CMD_RD_START:
            if (dest == CAN_NODE_ADDR) {
                can_dbg_wr_abort();
                can_dbg_rd_start(&f);
            }
            break;

        case CAN_DBG_CMD_WR_START:
            if (dest == CAN_NODE_ADDR) {
                can_dbg_rd_abort();
                can_dbg_wr_start(&f);
            }
            break;

        case CAN_DBG_CMD_WR_DATA:
            if (dest == CAN_NODE_ADDR) {
                can_dbg_wr_data(&f);
            }
            break;

        case CAN_DBG_CMD_CRC_VERIFY:
            if (dest == CAN_NODE_ADDR) {
                can_dbg_crc_start(&f);
            }
            break;

        default:
            break; /* 其余 cmd 号整帧忽略 */
        }
    }

    /* 调试内存读写的事务推进：读流发送 / 写等待超时 */
    can_dbg_rd_periodic();
    can_dbg_wr_periodic();
}
