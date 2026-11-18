/**
 * @file    keypad.c
 * @brief   4×4 矩阵键盘驱动（行 PB12–PB15，列 PB3–PB6）
 *
 * 扫描方式：
 *   1. 行线配置为开漏输出并开启内部上拉，默认全部释放（高电平）。
 *   2. 逐行拉低，延时等待分布电容稳定，读取 4 根列线。
 *   3. 某一列读到低电平，说明该行与该列交叉处的按键按下。
 *   4. 20ms 扫描一次，连续 2 次读到同一键值才认可（40ms 消抖）。
 */

#include "keypad.h"

/* 行线：KEYS1–KEYS4 */
#define KEYPAD_ROW_PORT   KEYS1_GPIO_Port
#define KEYPAD_ROW_PINS   (KEYS1_Pin | KEYS2_Pin | KEYS3_Pin | KEYS4_Pin)

/* 列线：KEYS5–KEYS8 */
#define KEYPAD_COL_PORT   KEYS5_GPIO_Port
#define KEYPAD_COL_PINS   (KEYS5_Pin | KEYS6_Pin | KEYS7_Pin | KEYS8_Pin)

#define KEYPAD_ROW_COUNT  4U
#define KEYPAD_COL_COUNT  4U
#define KEYPAD_DEBOUNCE   2U

static const uint16_t s_row_pin[KEYPAD_ROW_COUNT] =
{
    KEYS1_Pin, KEYS2_Pin, KEYS3_Pin, KEYS4_Pin
};

static const uint16_t s_col_pin[KEYPAD_COL_COUNT] =
{
    KEYS5_Pin, KEYS6_Pin, KEYS7_Pin, KEYS8_Pin
};

/* 实测：键盘 8 根线按 C4,C3,C2,C1,R4,R3,R2,R1 接入（FPC 反插），
 * 扫描得到的 r 对应物理列（反向），c 对应物理行（反向）。
 * 换算成物理行优先索引：0=左上角“1”，15=右下角“D”，
 * 这样屏幕 4×4 网格与键盘丝印方向一致。 */
#define KEYPAD_PHYS_INDEX(r, c)  ((uint8_t)(((3U - (c)) * 4U) + (3U - (r))))

/* 键位：1 2 3 A / 4 5 6 B / 7 8 9 C / * 0 # D
 * 若以后更换键盘、行列方向不同，只改这张表即可。 */
static const char s_key_char[KEYPAD_ROW_COUNT * KEYPAD_COL_COUNT] =
{
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'
};

static uint32_t s_scan_tick   = 0U;
static uint8_t  s_raw_last    = KEYPAD_KEY_NONE;
static uint8_t  s_stable_cnt  = 0U;
static uint8_t  s_key         = KEYPAD_KEY_NONE;
static uint8_t  s_event       = KEYPAD_KEY_NONE;

/* 行线拉低后等待电平稳定（约几微秒，足够 40kΩ 上拉给线容充电） */
static void keypad_settle(void)
{
    volatile uint32_t n;

    for (n = 0U; n < 400U; n++)
    {
        __NOP();
    }
}

static void keypad_release_rows(void)
{
    KEYPAD_ROW_PORT->BSRR = (uint32_t)KEYPAD_ROW_PINS;
}

/* 返回 0–15 的交叉点索引，无按键返回 0xFF */
static uint8_t keypad_read_raw(void)
{
    uint8_t  r;
    uint8_t  c;
    uint16_t cols;

    for (r = 0U; r < KEYPAD_ROW_COUNT; r++)
    {
        keypad_release_rows();
        KEYPAD_ROW_PORT->BRR = s_row_pin[r];     /* 只把当前行拉低 */
        keypad_settle();

        cols = (uint16_t)(~KEYPAD_COL_PORT->IDR) & KEYPAD_COL_PINS;
        if (cols != 0U)
        {
            for (c = 0U; c < KEYPAD_COL_COUNT; c++)
            {
                if ((cols & s_col_pin[c]) != 0U)
                {
                    keypad_release_rows();
                    return KEYPAD_PHYS_INDEX(r, c);
                }
            }
        }
    }

    keypad_release_rows();
    return KEYPAD_KEY_NONE;
}

void KEYPAD_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* PB3/PB4 默认是 JTAG 的 JTDO/NJTRST，关闭 JTAG 后才能当 GPIO 用 */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* 行线：开漏输出 + 内部上拉，未选中时为高电平 */
    gpio.Pin   = (uint32_t)KEYPAD_ROW_PINS;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(KEYPAD_ROW_PORT, &gpio);
    keypad_release_rows();

    /* 列线：上拉输入，按下时被行线拉低 */
    gpio.Pin   = (uint32_t)KEYPAD_COL_PINS;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(KEYPAD_COL_PORT, &gpio);

    s_scan_tick  = HAL_GetTick();
    s_raw_last   = KEYPAD_KEY_NONE;
    s_stable_cnt = 0U;
    s_key        = KEYPAD_KEY_NONE;
    s_event      = KEYPAD_KEY_NONE;
}

void KEYPAD_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t  raw;

    if ((now - s_scan_tick) < KEYPAD_SCAN_INTERVAL_MS)
    {
        return;
    }
    s_scan_tick = now;

    raw = keypad_read_raw();

    if (raw == s_raw_last)
    {
        if (s_stable_cnt < 0xFFU)
        {
            s_stable_cnt++;
        }
    }
    else
    {
        s_raw_last   = raw;
        s_stable_cnt = 0U;
    }

    if (s_stable_cnt >= KEYPAD_DEBOUNCE)
    {
        if (raw != s_key)
        {
            if (raw != KEYPAD_KEY_NONE)
            {
                s_event = raw;              /* 记录一次新的按下事件 */
            }
            s_key = raw;
        }
    }
}

uint8_t KEYPAD_GetKey(void)
{
    return s_key;
}

uint8_t KEYPAD_TakeEvent(void)
{
    uint8_t evt = s_event;

    s_event = KEYPAD_KEY_NONE;
    return evt;
}

char KEYPAD_GetChar(uint8_t index)
{
    if (index >= (KEYPAD_ROW_COUNT * KEYPAD_COL_COUNT))
    {
        return '-';
    }
    return s_key_char[index];
}
