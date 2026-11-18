/**
 * @file    http_client.h
 * @brief   基于 ESP8266 AT 指令的 HTTP 上报（医生端 → 服务端）
 *
 * 依赖：ESP8266 已连上 WiFi（ESP8266_IsConnected() 为 1）。
 * 行为：队列版本变化时上报 /api/device/call，其余时间每 5 秒发一次心跳。
 *       WiFi 未连接时保留最新一条请求，连上后自动补发。
 */

#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 服务端地址：填电脑 / 树莓派的局域网 IP，必须与 ESP8266 所连 WiFi 同网段 */
#define HTTP_SERVER_HOST   "192.168.123.151"
#define HTTP_SERVER_PORT   5000U

/* 诊室编号，与服务端 room 字段一致 */
#define HTTP_ROOM          "01"

#define HTTP_HEARTBEAT_MS  5000U

void    HTTP_Init(void);
void    HTTP_Task(void);

/* 按键调试上报：把队列/HTTP/WiFi 关键状态打包 POST 到 /api/device/debug。
   低优先级，不覆盖叫号/心跳请求，仅用于问题排查。 */
void    HTTP_PostDebug(const char *key);

/* 正在收发中返回 1，调试或低功耗场景可用 */
uint8_t HTTP_IsBusy(void);

/* 服务端同步回来的显示数据（以服务端为准，每 5 秒刷新一次） */
extern uint16_t HTTP_SrvWaiting;   /* 等候人数 */
extern uint16_t HTTP_SrvPassed;    /* 过号人数 */
extern uint8_t  HTTP_SrvOnline;    /* 终端是否在线（0/1） */

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_CLIENT_H__ */
