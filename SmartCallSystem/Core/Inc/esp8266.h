/**
 * @file    esp8266.h
 * @brief   ESP8266 AT 固件驱动（USART2，115200）
 *
 * 功能：非阻塞初始化、AT+CWLAP 扫描、结果缓存、周期性自动重扫。
 * 接线：PA2(USART2_TX) -> ESP RX，PA3(USART2_RX) -> ESP TX，GND 共地。
 */

#ifndef __ESP8266_H__
#define __ESP8266_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define ESP8266_RX_BUFFER_SIZE   4096U
#define ESP8266_LINE_BUFFER_SIZE 160U
#define ESP8266_MAX_AP           20U
#define ESP8266_SSID_MAX         33U
#define ESP8266_MAC_MAX          18U

#define ESP8266_SCAN_INTERVAL_MS 2000U
#define ESP8266_SCAN_TIMEOUT_MS  10000U
#define ESP8266_AT_TIMEOUT_MS    1500U
#define ESP8266_RETRY_INTERVAL_MS 3000U

/* 扫描到目标 SSID 后自动连接：请改成你自己的 2.4G WiFi 名称和密码 */
#define ESP8266_TARGET_SSID      "YOUR_WIFI_SSID"
#define ESP8266_TARGET_PASS      "YOUR_WIFI_PASSWORD"
#define ESP8266_CONNECT_TIMEOUT_MS 20000U

typedef struct
{
    char    ssid[ESP8266_SSID_MAX];
    char    mac[ESP8266_MAC_MAX];
    int8_t  rssi;
    uint8_t channel;
    uint8_t ecn;
} ESP8266_APInfo_t;

void     ESP8266_Init(void);
void     ESP8266_Process(void);
void     ESP8266_StartScan(void);
uint8_t  ESP8266_IsReady(void);
uint8_t  ESP8266_IsScanning(void);
uint8_t  ESP8266_IsConnecting(void);
uint8_t  ESP8266_IsConnected(void);
uint8_t  ESP8266_HasConnectError(void);
uint8_t  ESP8266_HasError(void);
uint16_t ESP8266_GetAPCount(void);
const ESP8266_APInfo_t *ESP8266_GetAP(uint16_t index);

/* 供上层协议（如 HTTP 客户端）直接发送原始字节，调用方自行携带 \r\n */
void     ESP8266_SendRaw(const uint8_t *data, uint16_t len);

/* 行回调：返回 1 表示该行已被上层消费，不再交给内部状态机 */
void     ESP8266_SetLineHook(uint8_t (*hook)(const char *line));

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_H__ */
