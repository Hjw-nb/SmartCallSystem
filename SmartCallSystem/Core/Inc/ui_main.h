/**
 * @file    ui_main.h
 * @brief   医生端主界面（顶栏 WiFi 状态 + 当前叫号 + 等候/过号 + 按键提示）
 */

#ifndef __UI_MAIN_H__
#define __UI_MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void UIMain_Init(void);
void UIMain_Task(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_MAIN_H__ */
