/**
 * @file    buzzer.c
 * @brief   非阻塞音调序列播放器：把提示音拆成音符序列，BUZZER_Task() 按时长推进
 *
 * TIM3 配置（CubeMX 生成）：PSC=71 → 计数频率 1MHz，PWM1，CH1=PA6。
 * 播放某个音调 = 设置 ARR = 1000000/freq - 1，CCR1 = (ARR+1)/2（50% 占空比）。
 * 停止 = 占空比清零，引脚保持空闲，避免 TDA7266 直流发热。
 */

#include "buzzer.h"

#define BUZZER_TIM_CLK_HZ 1000000UL   /* 72MHz / (PSC 71 + 1) */
#define BUZZER_MAX_NOTES  4U

typedef struct
{
    uint16_t freq;      /* Hz，0 表示休止符 */
    uint16_t duration;  /* ms */
} BuzzerNote_t;

extern TIM_HandleTypeDef htim3;

static BuzzerNote_t s_notes[BUZZER_MAX_NOTES];
static uint8_t      s_len      = 0U;
static uint8_t      s_index    = 0U;
static uint8_t      s_playing  = 0U;
static uint32_t     s_note_tick = 0U;

static void buzzer_tone(uint16_t freq)
{
    uint32_t arr;

    if (freq == 0U)
    {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
        return;
    }

    arr = (BUZZER_TIM_CLK_HZ / (uint32_t)freq) - 1UL;
    __HAL_TIM_SET_AUTORELOAD(&htim3, arr);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, (arr + 1UL) / 2UL);
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
}

static void buzzer_play(const BuzzerNote_t *notes, uint8_t len)
{
    uint8_t i;

    if ((notes == NULL) || (len == 0U) || (len > BUZZER_MAX_NOTES))
    {
        return;
    }

    for (i = 0U; i < len; i++)
    {
        s_notes[i] = notes[i];
    }
    s_len       = len;
    s_index     = 0U;
    s_playing   = 1U;
    s_note_tick = HAL_GetTick();

    buzzer_tone(s_notes[0].freq);
}

void BUZZER_Init(void)
{
    s_len     = 0U;
    s_index   = 0U;
    s_playing = 0U;

    (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
}

void BUZZER_Task(void)
{
    uint32_t now;

    if (s_playing == 0U)
    {
        return;
    }

    now = HAL_GetTick();
    if ((now - s_note_tick) < (uint32_t)s_notes[s_index].duration)
    {
        return;
    }

    s_index++;
    s_note_tick = now;

    if (s_index >= s_len)
    {
        s_playing = 0U;
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
        return;
    }

    buzzer_tone(s_notes[s_index].freq);
}

void BUZZER_Click(void)
{
    static const BuzzerNote_t seq[] = { {2000U, 60U} };
    buzzer_play(seq, (uint8_t)(sizeof(seq) / sizeof(seq[0])));
}

void BUZZER_Call(void)
{
    static const BuzzerNote_t seq[] = { {880U, 150U}, {0U, 40U}, {660U, 180U} };
    buzzer_play(seq, (uint8_t)(sizeof(seq) / sizeof(seq[0])));
}

void BUZZER_Confirm(void)
{
    static const BuzzerNote_t seq[] = { {1200U, 80U} };
    buzzer_play(seq, (uint8_t)(sizeof(seq) / sizeof(seq[0])));
}

void BUZZER_Deny(void)
{
    static const BuzzerNote_t seq[] = { {200U, 300U} };
    buzzer_play(seq, (uint8_t)(sizeof(seq) / sizeof(seq[0])));
}
