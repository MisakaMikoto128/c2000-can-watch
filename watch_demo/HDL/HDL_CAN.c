/* HDL_CAN.c - CAN 帧收发，跑在 F280025 的 DCAN-A 上。
 *
 * 收发引脚是 GPIO12(CANA_RX) / GPIO13(CANA_TX)；CAN 的位时钟直接取 100MHz 的
 * SYSCLK（CLKSRCCTL2.CANABCLKSEL=0 选 PERx.SYSCLK）。
 * 波特率由 HDL_CAN_Init(baud) 的档位参数选择，各档 BRP/TSEG 值见 s_baud_tab；
 * 默认 125 kbps，每位 20 tq，采样点 70%。
 * 32 个消息对象中 1~16 号做接收（每个暂存一帧），17~32 号做发送；
 * 收发两个软件队列各 8 帧深，cqueue 要靠多出来的一个空槽判满，所以申请 8+1 个缓冲。
 */
#include "HDL_CAN.h"
#include "cqueue.h"
#include "HDL_CPU_Time.h"
#include "CPU_Define.h"

static void HDL_CAN_RxPush(uint32_t id, uint16_t xtd, const uint16_t *data, uint16_t dlc);

/* 软件接收过滤：只有满足 (id & mask) == (filter_id & mask) 的帧才入队，默认不启用（全收） */
static uint16_t s_filter_en = 0u;
static uint32_t s_filter_id = 0u;
static uint32_t s_filter_mask = 0u;

void HDL_CAN_SetRxFilter(uint32_t filter_id, uint32_t mask)
{
    s_filter_id = filter_id;
    s_filter_mask = mask;
    s_filter_en = 1u;
}

void HDL_CAN_ClearRxFilter(void)
{
    s_filter_en = 0u;
}

/* CAN 丢帧统计 */
static struct
{
    volatile uint32_t rx_queue_drop;   // RX 软件队列满丢帧计数
    volatile uint32_t tx_queue_drop;   // 发送软件队列满丢帧计数
    volatile uint32_t rx_msglst;       // 硬件 RX mailbox 覆盖丢帧计数 (MsgLst)
    volatile uint32_t tx_submit_fail;  // 发送时 IF1 忙等超时丢帧计数（硬件无响应）
} s_can_err_stat = {0u, 0u, 0u, 0u};

/* 各档位位时序：CAN 位时钟 = SYSCLK = 100MHz。
 * 总 tq = 1 + (TSEG1+1) + (TSEG2+1)，波特率 = 100MHz / ((BRP+1) × 总tq)；
 * 四档都取 20 tq，采样点 = (1 + (TSEG1+1)) / 20 = 70%。
 * 新增档位：加一行，保持总 tq 与采样点不变，波特率只随 BRP 变。 */
static const uint16_t s_baud_tab[CAN_BAUD_NUM][4] = {
    /*                 BRP, SJW, TSEG1, TSEG2 */
    [CAN_BAUD_125K]  = {39u, 3u, 12u, 5u},   /* (39+1)×20 tq @100MHz = 125 kbps */
    [CAN_BAUD_250K]  = {19u, 3u, 12u, 5u},   /* (19+1)×20 tq @100MHz = 250 kbps */
    [CAN_BAUD_500K]  = { 9u, 3u, 12u, 5u},   /* ( 9+1)×20 tq @100MHz = 500 kbps */
    [CAN_BAUD_1000K] = { 4u, 3u, 12u, 5u},   /* ( 4+1)×20 tq @100MHz = 1 Mbps   */
};

uint32_t HDL_CAN_GetRxQueueDropCount(void) { return s_can_err_stat.rx_queue_drop; }
uint32_t HDL_CAN_GetTxQueueDropCount(void) { return s_can_err_stat.tx_queue_drop; }
uint32_t HDL_CAN_GetRxMsgLstCount(void)   { return s_can_err_stat.rx_msglst; }
uint32_t HDL_CAN_GetTxSubmitFailCount(void) { return s_can_err_stat.tx_submit_fail; }

/* RX 软件队列：元素直接复用对外的帧类型，省得两份布局各改各的会对不上。
 * cqueue 需要一个空槽来判满，所以缓冲比队列深度多一个。 */
#define HDL_CAN_RX_Q_DEPTH 8u
static HDL_CAN_Frame_t s_rxQ_buf[HDL_CAN_RX_Q_DEPTH + 1u];
static CQueue_t s_rxQ;

/* TX 软件队列 + 硬件发送邮箱。
 * Write 系列先入软件队列，HDL_CAN_TxPoll 再挑空闲的 TX 邮箱把帧交出去；
 * 16 个发送邮箱轮着用，已经在总线上的帧不会被新帧盖掉。 */
#define CAN_RX_MBOX_BASE   1u
#define CAN_RX_MBOX_NUM    16u
#define CAN_TX_MBOX_BASE   17u
#define CAN_TX_MBOX_NUM    16u

#define HDL_CAN_TX_Q_DEPTH 8u
static HDL_CAN_Frame_t s_txQ_buf[HDL_CAN_TX_Q_DEPTH + 1u];
static CQueue_t s_txQ;

/* 等 IF 寄存器空闲的最长时间（us，用 HDL_CPU_Time 的 1us 时基），超了就返回 0 */
#define CAN_IF_WAIT_TIMEOUT_US 1000u

static uint16_t HDL_CAN_If1Wait(void)
{
    UsTimer_t tickstart = HDL_CPU_Time_GetUsTick();
    while(CanaRegs.CAN_IF1CMD.bit.Busy == 1u)
    {
        if((HDL_CPU_Time_GetUsTick() - tickstart) >= CAN_IF_WAIT_TIMEOUT_US)
            return 0u;
    }
    return 1u;
}
static uint16_t HDL_CAN_If2Wait(void)
{
    UsTimer_t tickstart = HDL_CPU_Time_GetUsTick();
    while(CanaRegs.CAN_IF2CMD.bit.Busy == 1u)
    {
        if((HDL_CPU_Time_GetUsTick() - tickstart) >= CAN_IF_WAIT_TIMEOUT_US)
            return 0u;
    }
    return 1u;
}

/* 找一个还没在发的 TX 邮箱（TxRqst 位为 0）并返回它的对象号，16 个都在忙就返回 0。
 * 对象 objID 的 TxRqst 是 CAN_TXRQ_21 的第 objID-1 位 */
static uint16_t HDL_CAN_TxFindFree(void)
{
    uint16_t i;
    for(i = 0u; i < CAN_TX_MBOX_NUM; i++)
    {
        uint16_t obj = CAN_TX_MBOX_BASE + i;
        if(((CanaRegs.CAN_TXRQ_21 >> (obj - 1u)) & 1u) == 0u)
            return obj;
    }
    return 0u;   // 全忙
}

/* 把一个帧填进指定 TX 邮箱并请求发送，只由 HDL_CAN_TxPoll 调用。
 * 标准帧的 11 位 ID 要挪到 ID 域的 bit28~18 才是硬件里的位置。
 * 返回 1 = 发送已请求；返回 0 = 等 IF1 超时，这一帧根本没写进去，丢帧由调用方计数 */
static uint16_t HDL_CAN_TxSubmit(uint16_t obj, uint32_t id, uint16_t xtd, const uint16_t *data, uint16_t len)
{
    union CAN_IF1CMD_REG cmd;

    if(len > 8u) len = 8u;

    // 先等 IF1 空下来；上一次触发发送没等完的部分，在这里一并等掉
    if(HDL_CAN_If1Wait() == 0u) return 0u;

    // ---- 1. 写 ID 到该 TX mailbox ----
    cmd.all = 0u;
    cmd.bit.MSG_NUM = obj;
    cmd.bit.Arb = 1u;
    cmd.bit.DIR = 1u;
    CanaRegs.CAN_IF1ARB.all = 0u;
    CanaRegs.CAN_IF1ARB.bit.MsgVal = 1u;
    CanaRegs.CAN_IF1ARB.bit.Xtd = (xtd != 0u) ? 1u : 0u;
    CanaRegs.CAN_IF1ARB.bit.Dir = 1u;
    CanaRegs.CAN_IF1ARB.bit.ID = (xtd != 0u) ? (id & 0x1FFFFFFFUL) : ((id & 0x7FFUL) << 18);
    CanaRegs.CAN_IF1CMD.all = cmd.all;
    // 同一个 IF 通道一次只能搬一件事，写数据之前得先让刚才那次传完
    if(HDL_CAN_If1Wait() == 0u) return 0u;

    // ---- 2. 写数据到 IF1 寄存器 ----
    if(len >= 1u) CanaRegs.CAN_IF1DATA.bit.Data_0 = data[0];
    if(len >= 2u) CanaRegs.CAN_IF1DATA.bit.Data_1 = data[1];
    if(len >= 3u) CanaRegs.CAN_IF1DATA.bit.Data_2 = data[2];
    if(len >= 4u) CanaRegs.CAN_IF1DATA.bit.Data_3 = data[3];
    if(len >= 5u) CanaRegs.CAN_IF1DATB.bit.Data_4 = data[4];
    if(len >= 6u) CanaRegs.CAN_IF1DATB.bit.Data_5 = data[5];
    if(len >= 7u) CanaRegs.CAN_IF1DATB.bit.Data_6 = data[6];
    if(len >= 8u) CanaRegs.CAN_IF1DATB.bit.Data_7 = data[7];

    // ---- 3. 设 DLC 并请求发送 ----
    CanaRegs.CAN_IF1MCTL.bit.DLC = len;
    cmd.all = 0u;
    cmd.bit.MSG_NUM = obj;
    cmd.bit.DIR     = 1u;
    cmd.bit.DATA_A  = (len >= 1u) ? 1u : 0u;
    cmd.bit.DATA_B  = (len > 4u)  ? 1u : 0u;
    cmd.bit.TXRQST  = 1u;
    cmd.bit.Control = 1u;
    CanaRegs.CAN_IF1CMD.all = cmd.all;
    // 触发完就返回，不在这里等发完；下次进 IF1 操作时再收尾
    return 1u;
}

/* 待发帧入软件队列（不直接发送），实际发送由 HDL_CAN_TxPoll 完成 */
static void HDL_CAN_TxEnqueue(uint32_t id, uint16_t xtd, const uint16_t *data, uint16_t dlc)
{
    HDL_CAN_Frame_t frame;
    uint16_t i;

    frame.id    = id;
    frame.xtd   = (xtd != 0u) ? 1u : 0u;
    frame.dlc   = (dlc > 8u) ? 8u : dlc;
    for(i = 0u; i < 8u; i++)
        frame.data[i] = (i < dlc) ? data[i] : 0u;

    if(cqueue_enqueue(&s_txQ, (CObject_t)&frame) == 0u)
    {
        s_can_err_stat.tx_queue_drop++;
    }
}

void HDL_CAN_WriteExt(uint32_t id, const uint16_t *data, uint16_t dlc)
{
    HDL_CAN_TxEnqueue(id, 1u, data, dlc);
}

void HDL_CAN_WriteFrame(const HDL_CAN_Frame_t *frame)
{
    if(frame == 0u) return;
    HDL_CAN_TxEnqueue(frame->id, frame->xtd, frame->data, frame->dlc);
}

/* 把发送队列帧填进空闲 TX mailbox（尽量填满），每主循环调用 */
static void HDL_CAN_TxPoll(void)
{
    HDL_CAN_Frame_t frame;

    while(cqueue_peek(&s_txQ, (CObject_t)&frame) != 0u)
    {
        uint16_t obj = HDL_CAN_TxFindFree();
        if(obj == 0u) break;   // 全部 TX mailbox 忙：等下轮
        if(HDL_CAN_TxSubmit(obj, frame.id, frame.xtd, frame.data, frame.dlc) == 0u)
            s_can_err_stat.tx_submit_fail++;   // 这一帧没发出去，照样出队，只记个数
        (void)cqueue_dequeue(&s_txQ, (CObject_t)&frame);
    }
}

uint16_t HDL_CAN_TxQueueFree(void)
{
    return (uint16_t)cqueue_residual_capacity(&s_txQ);
}

bool HDL_CAN_TxAllIdle(void)
{
    // TX 邮箱是对象 17~32，对应 CAN_TXRQ_21 的 bit16~31
    return ((CanaRegs.CAN_TXRQ_21 >> (CAN_TX_MBOX_BASE - 1u)) & 0xFFFFu) == 0u;
}

/* 依次开 CAN 时钟、把 GPIO12/13 切到 CAN 功能、软复位模块、设位时序、配收发邮箱、
 * 建两个软件队列，最后退出初始化模式 */
void HDL_CAN_Init(HDL_CAN_Baud_t baud)
{
    const uint16_t *bt;

    if ((uint16_t)baud >= (uint16_t)CAN_BAUD_NUM) {
        baud = CAN_BAUD_125K;   // 非法档位兜底
    }
    bt = s_baud_tab[baud];

    // ---- 1. 开 CAN 时钟 ----
    EALLOW;
    CpuSysRegs.PCLKCR10.bit.CAN_A = 1u;
    ClkCfgRegs.CLKSRCCTL2.bit.CANABCLKSEL = 0u;   // CAN 位时钟 = PERx.SYSCLK (100MHz)
    EDIS;

    // ---- 2. GPIO12=CANA_RX, GPIO13=CANA_TX（mux1 外设功能） ----
    EALLOW;
    GpioCtrlRegs.GPAGMUX1.bit.GPIO12 = 3u;
    GpioCtrlRegs.GPAMUX1.bit.GPIO12  = 1u;
    GpioCtrlRegs.GPADIR.bit.GPIO12   = 0u;   // 输入
    GpioCtrlRegs.GPAPUD.bit.GPIO12   = 1u;
    GpioCtrlRegs.GPAQSEL1.bit.GPIO12 = 3u;   // 异步
    GpioCtrlRegs.GPAGMUX1.bit.GPIO13 = 3u;
    GpioCtrlRegs.GPAMUX1.bit.GPIO13  = 1u;
    GpioCtrlRegs.GPADIR.bit.GPIO13   = 1u;   // 输出
    GpioCtrlRegs.GPAPUD.bit.GPIO13   = 1u;
    EDIS;

    // ---- 3. 复位模块并进入初始化模式 ----
    /* SWR 是软件复位位，写 1 之后要维持一段时间再写 0，复位才真正做完，硬件不会自清。
     * 全新上电时复位没做够也没什么状态要清，但从 Bootloader 交接过来的模块带着运行态，
     * 消息 RAM 是脏的：复位时间不够就往下配，后面每次写 IF1 邮箱都会丢，发出去的是
     * ID=0、DLC=0 的空帧，接收一帧也收不到。所以这里留足 1ms 让复位做完再配。 */
    EALLOW;
    CanaRegs.CAN_CTL.bit.Init = 1u;
    CanaRegs.CAN_CTL.bit.SWR = 1u;
    EDIS;
    DELAY_US(1000u);
    EALLOW;
    CanaRegs.CAN_CTL.bit.SWR = 0u;
    EDIS;
    DELAY_US(1000u);

    EALLOW;
    CanaRegs.CAN_CTL.bit.PMD = 5u;          // 关闭消息 RAM 奇偶校验（0x5 关，其余值开）
    CanaRegs.CAN_CTL.bit.Init = 1u;         // 进入初始化模式
    EDIS;

    // 初始化消息对象 RAM
    EALLOW;
    CanaRegs.CAN_RAM_INIT.all = 0x1Au;      // KEY=0xA + CAN_RAM_INIT=1
    EDIS;
    while(CanaRegs.CAN_RAM_INIT.bit.RAM_INIT_DONE != 1u) { }
    EALLOW;
    CanaRegs.CAN_RAM_INIT.all = 0u;
    EDIS;

    // 进入初始化并允许配置位时序
    EALLOW;
    CanaRegs.CAN_CTL.bit.Init = 1u;
    CanaRegs.CAN_CTL.bit.CCE = 1u;
    // ---- 位时序：取 s_baud_tab 里本档位的值 ----
    CanaRegs.CAN_BTR.all = 0u;
    CanaRegs.CAN_BTR.bit.BRP   = bt[0];
    CanaRegs.CAN_BTR.bit.SJW   = bt[1];
    CanaRegs.CAN_BTR.bit.TSEG1 = bt[2];     // (TSEG1+1) tq
    CanaRegs.CAN_BTR.bit.TSEG2 = bt[3];     // (TSEG2+1) tq，采样点 70%
    CanaRegs.CAN_CTL.bit.ABO = 1u;          // 关掉总线后自动重新接入
    CanaRegs.CAN_CTL.bit.DAR = 0u;          // 发送失败自动重发
    CanaRegs.CAN_CTL.bit.EIE = 1u;          // 错误中断使能
    CanaRegs.CAN_CTL.bit.SIE = 0u;          // 状态中断不使能
    CanaRegs.CAN_CTL.bit.CCE = 0u;
    CanaRegs.CAN_CTL.bit.Init = 0u;
    EDIS;
    DELAY_US(1000u);

    // ---- 4. 配 1~16 号接收邮箱：什么都不过滤，什么帧都收 ----
    {
        uint16_t m;
        for(m = 0u; m < CAN_RX_MBOX_NUM; m++)
        {
            union CAN_IF1CMD_REG cmd;

            // IF1 等超时就是配不进去了，剩下的邮箱不再配：配到一半的接收链比空的还难查
            if(HDL_CAN_If1Wait() == 0u) break;

            cmd.all = 0u;
            cmd.bit.MSG_NUM = CAN_RX_MBOX_BASE + m;
            cmd.bit.Control = 1u;
            cmd.bit.Arb = 1u;
            cmd.bit.Mask = 1u;
            cmd.bit.DATA_A = 1u;
            cmd.bit.DATA_B = 1u;
            cmd.bit.DIR = 1u;   // 方向：往邮箱里写

            // 掩码全 0 配 UMask=1：不参与比较，任何 ID 都能进这个邮箱
            CanaRegs.CAN_IF1MSK.all = 0u;
            CanaRegs.CAN_IF1MSK.bit.Msk  = 0x0u;
            CanaRegs.CAN_IF1MSK.bit.MDir = 0u;
            CanaRegs.CAN_IF1MSK.bit.MXtd = 0u;

            CanaRegs.CAN_IF1ARB.all = 0u;
            CanaRegs.CAN_IF1ARB.bit.MsgVal = 1u;
            CanaRegs.CAN_IF1ARB.bit.Xtd = 1u;       // 收 29 位扩展帧
            CanaRegs.CAN_IF1ARB.bit.Dir = 0u;       // 接收邮箱
            CanaRegs.CAN_IF1ARB.bit.ID = 0u;

            CanaRegs.CAN_IF1MCTL.all = 0u;
            CanaRegs.CAN_IF1MCTL.bit.DLC = 8u;
            /* EoB 链接出 16 邮箱接收 FIFO：DCAN 消息处理器把来帧存进块内
             * 编号最小的空闲（NewDat=0）邮箱，只有 16 个全满才丢帧计 MsgLst。
             * 此前每个邮箱都 EoB=1（各自成块），帧永远进 1 号邮箱——主循环
             * 扫窗期间后到的帧被顶掉（实测背靠背三联帧只有首帧有应答）。
             * 规则（driverlib can.c）：FIFO 成员 EoB=0，块尾才 EoB=1。 */
            CanaRegs.CAN_IF1MCTL.bit.EoB = (m == CAN_RX_MBOX_NUM - 1u) ? 1u : 0u;
            CanaRegs.CAN_IF1MCTL.bit.NewDat = 0u;
            CanaRegs.CAN_IF1MCTL.bit.IntPnd = 0u;
            CanaRegs.CAN_IF1MCTL.bit.UMask = 1u;    // 只能最后写，先写会被上面的 all=0 清掉

            CanaRegs.CAN_IF1CMD.all = cmd.all;      // 这一笔下去才把上面几条真正提交进邮箱
            // 不等这次传完，下一轮进 IF1 操作时再收尾
        }
    }

    // ---- 5. 配 17~32 号发送邮箱 ----
    {
        uint16_t t;
        for(t = 0u; t < CAN_TX_MBOX_NUM; t++)
        {
            union CAN_IF1CMD_REG cmd;
            uint16_t obj = CAN_TX_MBOX_BASE + t;

            if(HDL_CAN_If1Wait() == 0u) break;
            cmd.all = 0u;
            cmd.bit.MSG_NUM = obj;
            cmd.bit.Arb = 1u;
            cmd.bit.Control = 1u;
            cmd.bit.DATA_A = 1u;
            cmd.bit.DATA_B = 1u;
            cmd.bit.DIR = 1u;

            CanaRegs.CAN_IF1ARB.all = 0u;
            CanaRegs.CAN_IF1ARB.bit.MsgVal = 1u;
            CanaRegs.CAN_IF1ARB.bit.Xtd = 1u;
            CanaRegs.CAN_IF1ARB.bit.Dir = 1u;       // 发送邮箱
            CanaRegs.CAN_IF1ARB.bit.ID = 0u;        // ID 空着，发的时候按帧填

            CanaRegs.CAN_IF1MCTL.all = 0u;
            CanaRegs.CAN_IF1MCTL.bit.DLC = 8u;
            CanaRegs.CAN_IF1MCTL.bit.RmtEn = 0u;

            CanaRegs.CAN_IF1CMD.all = cmd.all;
        }
    }

    // ---- 6. 建接收、发送两个软件队列 ----
    cqueue_create(&s_rxQ, (CObject_t)s_rxQ_buf,
                  HDL_CAN_RX_Q_DEPTH + 1u, sizeof(HDL_CAN_Frame_t));
    cqueue_create(&s_txQ, (CObject_t)s_txQ_buf,
                  HDL_CAN_TX_Q_DEPTH + 1u, sizeof(HDL_CAN_Frame_t));

    // ---- 7. 退出初始化模式，开始收发 ----
    EALLOW;
    CanaRegs.CAN_CTL.bit.Init = 0u;
    EDIS;
}

/* 挨个查 16 个接收邮箱，哪个邮箱里躺着新帧就读出来入队 */
static void HDL_CAN_RxPoll(void)
{
    uint16_t m;
    for(m = 0u; m < CAN_RX_MBOX_NUM; m++)
    {
        union CAN_IF2CMD_REG cmd;
        uint32_t rx_id;
        uint16_t dlc;
        uint16_t data[8];

        // 这里不等 IF2 空闲：上一轮读完之后已经等过一次，通道是空的

        // 把这个邮箱的状态、ID、数据一次读进 IF2 寄存器
        cmd.all = 0u;
        cmd.bit.MSG_NUM = CAN_RX_MBOX_BASE + m;
        cmd.bit.Control = 1u;
        cmd.bit.Arb = 1u;
        cmd.bit.DATA_A = 1u;
        cmd.bit.DATA_B = 1u;
        cmd.bit.DIR = 0u;   // 方向：从邮箱里读
        CanaRegs.CAN_IF2CMD.all = cmd.all;
        (void)HDL_CAN_If2Wait();

        if(CanaRegs.CAN_IF2MCTL.bit.NewDat == 1u)
        {
            uint16_t msg_lost = CanaRegs.CAN_IF2MCTL.bit.MsgLst;   // 邮箱被后到的帧顶掉的标志，读过要清
            uint16_t rx_xtd = CanaRegs.CAN_IF2ARB.bit.Xtd;

            rx_id = CanaRegs.CAN_IF2ARB.bit.ID;
            /* 标准帧 11-bit ID 硬件存放于 ID 域 bits28-18，规范化到低位 */
            if(rx_xtd == 0u) rx_id >>= 18;
            dlc   = CanaRegs.CAN_IF2MCTL.bit.DLC;
            if(dlc > 8u) dlc = 8u;
            data[0] = CanaRegs.CAN_IF2DATA.bit.Data_0;
            data[1] = CanaRegs.CAN_IF2DATA.bit.Data_1;
            data[2] = CanaRegs.CAN_IF2DATA.bit.Data_2;
            data[3] = CanaRegs.CAN_IF2DATA.bit.Data_3;
            data[4] = CanaRegs.CAN_IF2DATB.bit.Data_4;
            data[5] = CanaRegs.CAN_IF2DATB.bit.Data_5;
            data[6] = CanaRegs.CAN_IF2DATB.bit.Data_6;
            data[7] = CanaRegs.CAN_IF2DATB.bit.Data_7;

            HDL_CAN_RxPush(rx_id, rx_xtd, data, dlc);

            if(msg_lost == 1u)
            {
                s_can_err_stat.rx_msglst++;
            }

            // 清 NewDat：留着的话下一轮 Poll 会把同一帧再读一遍
            cmd.all = 0u;
            cmd.bit.MSG_NUM = CAN_RX_MBOX_BASE + m;
            cmd.bit.TxRqst = 1u;
            CanaRegs.CAN_IF2CMD.all = cmd.all;
            (void)HDL_CAN_If2Wait();

            // 清 MsgLst（该位写 1 清除）：把整个 MCTL 原样写回邮箱，只有 MsgLst 置 1，
            // 邮箱之间的接收链设置不动
            if(msg_lost == 1u)
            {
                CanaRegs.CAN_IF2MCTL.bit.NewDat = 0u;
                CanaRegs.CAN_IF2MCTL.bit.MsgLst = 1u;
                cmd.all = 0u;
                cmd.bit.MSG_NUM = CAN_RX_MBOX_BASE + m;
                cmd.bit.Control = 1u;
                cmd.bit.DIR = 1u;
                CanaRegs.CAN_IF2CMD.all = cmd.all;
                (void)HDL_CAN_If2Wait();
            }
        }
    }
}

/* 收一遍邮箱，再发一遍队列 */
void HDL_CAN_Poll(void)
{
    HDL_CAN_RxPoll();
    HDL_CAN_TxPoll();
}

/* 软件过滤命中的帧进接收队列 */
static void HDL_CAN_RxPush(uint32_t id, uint16_t xtd, const uint16_t *data, uint16_t dlc)
{
    HDL_CAN_Frame_t frame;
    uint16_t i;

    if((s_filter_en != 0u) && ((id & s_filter_mask) != (s_filter_id & s_filter_mask)))
        return;

    frame.id    = id;
    frame.xtd   = (xtd != 0u) ? 1u : 0u;
    frame.dlc   = (dlc > 8u) ? 8u : dlc;
    for(i = 0u; i < 8u; i++)
        frame.data[i] = (i < dlc) ? data[i] : 0u;

    if(cqueue_enqueue(&s_rxQ, (CObject_t)&frame) == 0u)
    {
        s_can_err_stat.rx_queue_drop++;
    }
}

/* 从接收队列取一帧填进 *frame；队列空或者 frame 传空都返回 false */
bool HDL_CAN_Read(HDL_CAN_Frame_t *frame)
{
    HDL_CAN_Frame_t qf;
    uint16_t i;

    if(frame == 0u) return false;
    if(cqueue_peek(&s_rxQ, (CObject_t)&qf) == 0u) return false;   // 队列空

    frame->id    = qf.id;
    frame->xtd   = qf.xtd;
    frame->dlc   = qf.dlc;
    for(i = 0u; i < HDL_CAN_DATA_LEN; i++)
        frame->data[i] = qf.data[i];
    (void)cqueue_dequeue(&s_rxQ, (CObject_t)&qf);
    return true;
}
