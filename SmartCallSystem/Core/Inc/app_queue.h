/**
 * @file    app_queue.h
 * @brief   医生端本地叫号队列（离线保底，不依赖服务器）
 *
 * 号码格式：字母前缀 + 三位序号，例如 A001。
 * 队列只保存在 RAM，掉电丢失；后续版本再加 Flash 存储。
 */

#ifndef __APP_QUEUE_H__
#define __APP_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define APP_QUEUE_MAX 100U

typedef enum
{
    TICKET_WAIT = 0,    /* 等候中 */
    TICKET_CALLING,     /* 当前正在叫 */
    TICKET_PASSED,      /* 已过号 */
    TICKET_DONE         /* 已就诊完成 */
} TicketState_t;

typedef struct
{
    uint8_t       prefix;   /* 号码前缀字母，例如 'A' */
    uint16_t      seq;      /* 序号 1-999 */
    TicketState_t state;
} Ticket_t;

void            QUEUE_Init(void);
uint8_t         QUEUE_Next(void);           /* 下一位（当前号置为完成） */
uint8_t         QUEUE_Recall(void);         /* 重呼当前号 */
uint8_t         QUEUE_Pass(void);           /* 当前号过号，并自动顺延 */
uint8_t         QUEUE_RecallPassed(void);   /* 回呼最近一个过号 */
uint8_t         QUEUE_TogglePause(void);    /* 暂停/继续，返回新的暂停状态 */
uint8_t         QUEUE_IsPaused(void);

/* 把网页端取的号追加到本地队列末尾（状态为等候）；
   队列已满或该序号已存在时返回 0，成功返回 1。 */
uint8_t         QUEUE_Append(uint8_t prefix, uint16_t seq);

/* 返回当前队列中最大的序号（用于向服务端请求“此后的新号”） */
uint16_t        QUEUE_GetMaxSeq(void);

const Ticket_t *QUEUE_GetCurrent(void);
uint16_t        QUEUE_GetWaitCount(void);
uint16_t        QUEUE_GetPassedCount(void);
uint16_t        QUEUE_GetCount(void);         /* 队列中号码总数（含各状态） */
uint32_t        QUEUE_GetVersion(void);     /* 队列内容变化计数，供界面判断重绘 */
uint8_t         QUEUE_TakeAnnounce(void);   /* 取走“需要闪烁播报”事件标志 */

/* 把号码格式化成 4 字符字符串（含结束符需 5 字节），例如 "A001" */
void            QUEUE_FormatCode(const Ticket_t *ticket, char *buf);

#ifdef __cplusplus
}
#endif

#endif /* __APP_QUEUE_H__ */
