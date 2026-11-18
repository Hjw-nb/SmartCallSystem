/**
 * @file    app_key.c
 * @brief   按键动作映射与“回呼过号”二次确认
 *
 * 确认窗口 3 秒：按 6 进入确认状态，3 秒内按 # 执行回呼，按 * 或超时取消。
 * 确认期间忽略其他按键，避免误触。
 */

#include "app_key.h"
#include "keypad.h"
#include "app_queue.h"
#include "buzzer.h"
#include "http_client.h"

#define APPKEY_CONFIRM_TIMEOUT_MS 3000U

static uint8_t  s_confirming  = 0U;
static uint32_t s_confirm_tick = 0U;

void APPKEY_Init(void)
{
    s_confirming   = 0U;
    s_confirm_tick = HAL_GetTick();
}

uint8_t APPKEY_IsConfirming(void)
{
    return s_confirming;
}

static void appkey_start_confirm(void)
{
    s_confirming   = 1U;
    s_confirm_tick = HAL_GetTick();
}

void APPKEY_Task(void)
{
    uint8_t event;
    char    ch;

    if (s_confirming != 0U)
    {
        if ((HAL_GetTick() - s_confirm_tick) >= APPKEY_CONFIRM_TIMEOUT_MS)
        {
            s_confirming = 0U;
        }
    }

    event = KEYPAD_TakeEvent();
    if (event == KEYPAD_KEY_NONE)
    {
        return;
    }

    ch = KEYPAD_GetChar(event);

    if (s_confirming != 0U)
    {
        if (ch == '#')
        {
            if (QUEUE_RecallPassed() != 0U)
            {
                BUZZER_Confirm();
            }
            else
            {
                BUZZER_Deny();
            }
            s_confirming = 0U;
        }
        else if (ch == '*')
        {
            BUZZER_Click();
            s_confirming = 0U;
        }
        else
        {
            BUZZER_Deny();
        }
        {
            char key_str[2] = { ch, '\0' };
            HTTP_PostDebug(key_str);
        }
        return;
    }

    switch (ch)
    {
        case '1':
            if (QUEUE_Next() != 0U)
            {
                BUZZER_Click();
            }
            else
            {
                BUZZER_Deny();
            }
            break;

        case '2':
            if (QUEUE_Recall() != 0U)
            {
                BUZZER_Click();
            }
            else
            {
                BUZZER_Deny();
            }
            break;

        case '3':
            if (QUEUE_Pass() != 0U)
            {
                BUZZER_Click();
            }
            else
            {
                BUZZER_Deny();
            }
            break;

        case '4':
            (void)QUEUE_TogglePause();
            BUZZER_Click();
            break;

        case '6':
            if (QUEUE_GetPassedCount() > 0U)
            {
                appkey_start_confirm();
                BUZZER_Click();
            }
            else
            {
                BUZZER_Deny();
            }
            break;

        default:
            BUZZER_Deny();
            break;
    }

    /* 每次按键后上报队列/HTTP/WiFi 状态，供服务端日志分析 */
    {
        char key_str[2] = { ch, '\0' };
        HTTP_PostDebug(key_str);
    }
}
