/**
 * @file    keypad_test.c
 * @brief   4×4 矩阵键盘对应关系测试界面
 *
 * 布局（128×160）：
 *   y=0    标题 KEYPAD TEST
 *   y=20   状态行：R? C?  KEY ?（按住时），松开后显示 LAST KEY ?
 *   y=44   4×4 键位网格，每格 28×24，按下的格子高亮为绿色
 *   y=142  底部提示：行线/列线所用引脚
 */

#include "keypad_test.h"
#include "keypad.h"
#include "st7735.h"
#include <stdio.h>
#include <string.h>

#define KT_STATUS_Y   20U
#define KT_GRID_X     8U
#define KT_GRID_Y     44U
#define KT_CELL_W     28U
#define KT_CELL_H     24U
#define KT_HINT_Y     142U

static uint8_t  s_shown_key    = KEYPAD_KEY_NONE;
static uint8_t  s_has_last     = 0U;
static char     s_last_char    = 0;
static uint8_t  s_status_dirty = 0U;
static uint32_t s_status_tick  = 0U;

static void kt_draw_cell(uint8_t index, uint8_t active)
{
    uint16_t x  = (uint16_t)(KT_GRID_X + ((index % 4U) * KT_CELL_W));
    uint16_t y  = (uint16_t)(KT_GRID_Y + ((index / 4U) * KT_CELL_H));
    uint16_t bg = (active != 0U) ? ST7735_GREEN : ST7735_BLACK;
    uint16_t fg = (active != 0U) ? ST7735_BLACK : ST7735_WHITE;

    ST7735_FillRect(x, y, KT_CELL_W, KT_CELL_H, bg);
    ST7735_DrawRect(x, y, KT_CELL_W, KT_CELL_H,
                    (active != 0U) ? ST7735_WHITE : ST7735_GRAY);
    ST7735_DrawChar((uint16_t)(x + 10U), (uint16_t)(y + 4U),
                    KEYPAD_GetChar(index), fg, bg, 1U);
}

static void kt_draw_status(uint8_t key)
{
    char text[20];
    uint16_t color = ST7735_WHITE;

    ST7735_FillRect(0U, KT_STATUS_Y, ST7735_WIDTH, 18U, ST7735_BLACK);

    if (key != KEYPAD_KEY_NONE)
    {
        (void)snprintf(text, sizeof(text), "R%u C%u  KEY %c",
                       (unsigned int)((key / 4U) + 1U),
                       (unsigned int)((key % 4U) + 1U),
                       KEYPAD_GetChar(key));
        color = ST7735_GREEN;
    }
    else if (s_has_last != 0U)
    {
        (void)snprintf(text, sizeof(text), "LAST KEY %c", s_last_char);
        color = ST7735_CYAN;
    }
    else
    {
        strcpy(text, "NO KEY");
        color = ST7735_GRAY;
    }

    ST7735_DrawString(4U, KT_STATUS_Y, text, color, ST7735_BLACK, 1U);
}

void KeypadTest_Init(void)
{
    uint8_t i;

    ST7735_FillScreen(ST7735_BLACK);

    ST7735_DrawString(4U, 0U, "KEYPAD TEST", ST7735_CYAN, ST7735_BLACK, 1U);
    ST7735_FillRect(0U, 17U, ST7735_WIDTH, 1U, ST7735_CYAN);
    ST7735_FillRect(0U, 40U, ST7735_WIDTH, 1U, ST7735_GRAY);
    ST7735_FillRect(0U, 141U, ST7735_WIDTH, 1U, ST7735_GRAY);

    for (i = 0U; i < 16U; i++)
    {
        kt_draw_cell(i, 0U);
    }

    ST7735_DrawString(8U, KT_HINT_Y, "PB12-15 PB3-6", ST7735_GRAY, ST7735_BLACK, 1U);

    s_shown_key    = KEYPAD_KEY_NONE;
    s_has_last     = 0U;
    s_last_char    = 0;
    s_status_dirty = 0U;
    s_status_tick  = HAL_GetTick();
    kt_draw_status(KEYPAD_KEY_NONE);
}

void KeypadTest_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t  key = KEYPAD_GetKey();
    uint8_t  evt = KEYPAD_TakeEvent();

    if (evt != KEYPAD_KEY_NONE)
    {
        s_last_char    = KEYPAD_GetChar(evt);
        s_has_last     = 1U;
        s_status_dirty = 1U;
    }

    if (key != s_shown_key)
    {
        if (s_shown_key != KEYPAD_KEY_NONE)
        {
            kt_draw_cell(s_shown_key, 0U);
        }
        if (key != KEYPAD_KEY_NONE)
        {
            kt_draw_cell(key, 1U);
        }
        s_shown_key    = key;
        s_status_dirty = 1U;
    }

    if ((s_status_dirty != 0U) && ((now - s_status_tick) >= 100U))
    {
        kt_draw_status(key);
        s_status_tick  = now;
        s_status_dirty = 0U;
    }
}
