/**
 * @file    jr6001.c
 * @brief   JR6001 语音模块驱动（ASCII 文本指令，USART1 = PA9/PA10，BUSY = PC7）
 *
 * 接线（2026-11-11 网表 U4）：
 *   U4-1 GND    → GND
 *   U4-2 VCC    → +5V（需 1A 级供电）
 *   U4-3 RX     → PA9  (USART1_TX，与 CN1 调试口共用)
 *   U4-4 TX     → PA10 (USART1_RX，回传 OK，本驱动不解析)
 *   U4-5 BUSY   → PC7  (播放时为高电平，空闲为低电平)
 *
 * 协议：9600 8N1，ASCII 文本，每条指令以 \r\n 结尾
 *   A2          播放当前曲目（从头）
 *   A3          暂停
 *   A5 / A6     上一曲 / 下一曲
 *   A7:00001    播放指定曲目（5 位文件名，对应 00001.wav）
 *   AF:30       设置音量（0~31）
 *   B0 / B1     音量加 / 音量减
 *
 * 注意：本模块不是 JQ8400！不能用 0xAA 二进制帧，必须发文本指令。
 *
 * 播放逻辑：发 A7 指令 → 等 BUSY 拉高（开播）→ 等 BUSY 回低（播完）→ 下一段。
 * 双超时兜底，BUSY 异常时也能按时序走完，不会卡死主循环。
 */

#include "jr6001.h"
#include <string.h>

extern UART_HandleTypeDef huart1;   /* CubeMX 已初始化，本驱动仅改波特率 */

#define JR_BUSY_PORT          GPIOC
#define JR_BUSY_PIN           GPIO_PIN_7

#define JR_VOLUME             31U     /* 0~31，31 为最大 */
#define JR_SEQ_MAX            12U

#define ST_IDLE               0U
#define ST_SEND               1U
#define ST_WAIT_START         2U
#define ST_WAIT_END           3U

#define JR_GAP_MS             100U    /* 段间留白，给模块复位解码器 */
#define JR_START_MS           700U    /* 等 BUSY 拉高的上限 */
#define JR_END_MARGIN_MS      900U    /* 片段时长之外的兜底余量 */

/* 各片段时长（ms），由生成脚本实测（22050Hz，已裁掉首尾静音），
 * 索引 1~18 依次对应 00001~00018 */
static const uint16_t s_clip_ms[19] =
{
    0U,
    270U, 190U, 450U, 260U,    /* 请 到 诊室 号 */
    210U, 140U, 300U, 140U,    /* A B C D */
    250U, 200U, 210U, 300U,    /* 零 一 二 三 */
    270U, 250U, 200U, 280U,    /* 四 五 六 七 */
    230U, 250U                 /* 八 九 */
};

static uint8_t  s_seq[JR_SEQ_MAX];
static uint8_t  s_seq_len = 0U;
static uint8_t  s_seq_pos = 0U;
static uint8_t  s_state   = ST_IDLE;
static uint32_t s_tick    = 0U;

/* 发送一条文本指令，自动补 \r\n */
static void jr_send_line(const char *s)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 20U);
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2U, 20U);
}

/* 设置音量：AF:30 */
static void jr_set_volume(uint8_t vol)
{
    char buf[8];

    if (vol > 31U)
    {
        vol = 31U;
    }

    buf[0] = 'A';
    buf[1] = 'F';
    buf[2] = ':';
    if (vol >= 10U)
    {
        buf[3] = (char)('0' + (vol / 10U));
        buf[4] = (char)('0' + (vol % 10U));
        buf[5] = '\0';
    }
    else
    {
        buf[3] = (char)('0' + vol);
        buf[4] = '\0';
    }
    jr_send_line(buf);
}

/* 播放指定曲目：A7:00001（5 位文件名） */
static void jr_play(uint16_t index)
{
    char buf[10];

    buf[0] = 'A';
    buf[1] = '7';
    buf[2] = ':';
    buf[3] = (char)('0' + ((index / 10000U) % 10U));
    buf[4] = (char)('0' + ((index / 1000U) % 10U));
    buf[5] = (char)('0' + ((index / 100U) % 10U));
    buf[6] = (char)('0' + ((index / 10U) % 10U));
    buf[7] = (char)('0' + (index % 10U));
    buf[8] = '\0';
    jr_send_line(buf);
}

void JR6001_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* PC7 = JRBUSY，模块主动输出，上拉仅作空闲保险 */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Pin  = JR_BUSY_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(JR_BUSY_PORT, &gpio);

    /* USART1 与 CN1 调试口共用，CubeMX 默认 115200，这里改为模块要求的 9600 */
    huart1.Init.BaudRate = 9600U;
    (void)HAL_UART_Init(&huart1);

    HAL_Delay(300U);            /* 等模块上电打印 "JR MUSIC V2.1" 自检完成 */
    jr_set_volume(JR_VOLUME);
    HAL_Delay(50U);

    s_seq_len = 0U;
    s_seq_pos = 0U;
    s_state   = ST_IDLE;
    s_tick    = HAL_GetTick();
}

uint8_t JR6001_IsBusy(void)
{
    return (s_state != ST_IDLE) ? 1U : 0U;
}

void JR6001_AnnounceTicket(uint8_t prefix, uint16_t seq)
{
    uint8_t  n = 0U;
    uint16_t v = seq;

    if (v > 999U) { v = 999U; }

    /* 请 */
    s_seq[n++] = 1U;

    /* 前缀字母 A-D -> 索引 5-8 */
    if ((prefix >= (uint8_t)'A') && (prefix <= (uint8_t)'D'))
    {
        s_seq[n++] = (uint8_t)(5U + (prefix - (uint8_t)'A'));
    }
    else
    {
        s_seq[n++] = 5U;
    }

    /* 三位数字 -> 索引 9-18 */
    s_seq[n++] = (uint8_t)(9U + ((v / 100U) % 10U));
    s_seq[n++] = (uint8_t)(9U + ((v / 10U) % 10U));
    s_seq[n++] = (uint8_t)(9U + (v % 10U));

    /* 号 到 一 诊室 */
    s_seq[n++] = 4U;
    s_seq[n++] = 2U;
    s_seq[n++] = 10U;
    s_seq[n++] = 3U;

    s_seq_len = n;
    s_seq_pos = 0U;
    s_state   = ST_SEND;
    s_tick    = HAL_GetTick();      /* 立即发第一段 */
}

void JR6001_Task(void)
{
    uint32_t now;
    uint8_t  busy;

    if (s_state == ST_IDLE)
    {
        return;
    }

    now  = HAL_GetTick();
    busy = (HAL_GPIO_ReadPin(JR_BUSY_PORT, JR_BUSY_PIN) == GPIO_PIN_SET) ? 1U : 0U;

    switch (s_state)
    {
    case ST_SEND:
        if ((int32_t)(now - s_tick) < 0)
        {
            break;
        }
        if (s_seq_pos >= s_seq_len)
        {
            s_state = ST_IDLE;
            break;
        }
        jr_play(s_seq[s_seq_pos]);
        s_tick  = now + JR_START_MS;
        s_state = ST_WAIT_START;
        break;

    case ST_WAIT_START:
        if (busy != 0U)
        {
            /* 已开播：按片段时长 + 余量兜底 */
            s_tick  = now + (uint32_t)s_clip_ms[s_seq[s_seq_pos]] + JR_END_MARGIN_MS;
            s_state = ST_WAIT_END;
        }
        else if ((int32_t)(now - s_tick) >= 0)
        {
            /* 一直没开播：跳过本段 */
            s_seq_pos++;
            s_tick  = now + JR_GAP_MS;
            s_state = ST_SEND;
        }
        break;

    case ST_WAIT_END:
        if ((busy == 0U) || ((int32_t)(now - s_tick) >= 0))
        {
            /* 播完或超时 → 下一段 */
            s_seq_pos++;
            s_tick  = now + JR_GAP_MS;
            s_state = ST_SEND;
        }
        break;

    default:
        s_state = ST_IDLE;
        break;
    }
}
