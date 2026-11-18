/**
 * @file    buzzer.h
 * @brief   TDA7266 音响提示音驱动（TIM3_CH1=PA6，非阻塞音调序列播放器）
 *
 * 接线：CN3-2(AD1) <- PA6，CN3-3(AD2) <- PA7（预留立体声），CN3-1/4 GND。
 * 原理：TIM3 计数频率 1MHz，修改 ARR 改变 PWM 频率得到不同音调，
 *       占空比固定 50%，输出经 TDA7266 放大驱动喇叭。
 */

#ifndef __BUZZER_H__
#define __BUZZER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void BUZZER_Init(void);
void BUZZER_Task(void);

void BUZZER_Click(void);    /* 按键短音 2kHz 60ms */
void BUZZER_Call(void);     /* 叫号提示 叮-咚（880Hz/660Hz 各 150ms） */
void BUZZER_Confirm(void);  /* 确认提示 1.2kHz 80ms */
void BUZZER_Deny(void);     /* 无效操作 200Hz 300ms */

#ifdef __cplusplus
}
#endif

#endif /* __BUZZER_H__ */
