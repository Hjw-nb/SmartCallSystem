/**
 * @file    app_key.h
 * @brief   医生端按键动作映射
 *
 * 矩阵键盘（默认映射）：
 *   1  下一位
 *   2  重呼当前号
 *   3  过号
 *   4  暂停 / 继续
 *   6  回呼过号（需二次确认）
 *   #  确认
 *   *  取消
 */

#ifndef __APP_KEY_H__
#define __APP_KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void    APPKEY_Init(void);
void    APPKEY_Task(void);
uint8_t APPKEY_IsConfirming(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_KEY_H__ */
