/**
 * @file    keypad.h
 * @brief   4×4 矩阵键盘驱动
 *
 * 接线（以最新网表为准）：
 *   行 KEYS1–KEYS4 -> PB12/PB13/PB14/PB15（开漏输出，扫描时逐行拉低）
 *   列 KEYS5–KEYS8 -> PB3/PB4/PB5/PB6（上拉输入，按下时被行线拉低）
 *
 * 注意：PB3/PB4 复位后是 JTAG 引脚，初始化时必须关闭 JTAG（保留 SWD）。
 */

#ifndef __KEYPAD_H__
#define __KEYPAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define KEYPAD_KEY_NONE        0xFFU
#define KEYPAD_SCAN_INTERVAL_MS 20U

void    KEYPAD_Init(void);
void    KEYPAD_Task(void);                       /* 主循环调用，内部 20ms 节拍 */
uint8_t KEYPAD_GetKey(void);                     /* 当前稳定按键 0–15，无按键 0xFF */
uint8_t KEYPAD_TakeEvent(void);                  /* 取一次“按下”事件，取后清空 */
char    KEYPAD_GetChar(uint8_t index);           /* 键值索引 -> 字符 */

#ifdef __cplusplus
}
#endif

#endif /* __KEYPAD_H__ */
