/**
 * @file    jr6001.h
 * @brief   JR6001 语音模块驱动（ASCII 文本指令，USART1 = PA9/PA10，BUSY = PC7）
 *
 * 接线（2026-11-11 网表 U4）：
 *   模块 RX   → PA9  (USART1_TX，与 CN1 调试口共用)
 *   模块 TX   → PA10 (USART1_RX，模块回 OK，本驱动不解析)
 *   模块 BUSY → PC7  (播放时高电平，空闲低电平)
 *   模块 VCC  → +5V（建议独立 5V/1A 供电），GND 共地
 *
 * 协议：9600 8N1，ASCII 文本，每条以 \r\n 结尾
 *   A2 播放 / A3 暂停 / A5 上一曲 / A6 下一曲
 *   A7:00001 播放指定曲目（5 位文件名）
 *   AF:30 设置音量（0~31） / B0 音量加 / B1 音量减
 *
 * 模块 Flash 根目录需且仅需 18 个音频文件，文件名 00001~00018 依次为：
 *   1=请 2=到 3=诊室 4=号 5=A 6=B 7=C 8=D
 *   9=零 10=一 11=二 12=三 13=四 14=五 15=六 16=七 17=八 18=九
 */

#ifndef __JR6001_H__
#define __JR6001_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void    JR6001_Init(void);
void    JR6001_Task(void);
uint8_t JR6001_IsBusy(void);

/* 播报排队号码：请 <prefix><seq 三位数字> 号 到 1 诊室 */
void    JR6001_AnnounceTicket(uint8_t prefix, uint16_t seq);

#ifdef __cplusplus
}
#endif

#endif /* __JR6001_H__ */
