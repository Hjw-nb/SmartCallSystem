/**
 * @file    esp8266.c
 * @brief   ESP8266 AT 固件驱动
 *
 * 说明：
 * 1. USART2 使用接收中断 + 环形缓冲，主循环解析。
 * 2. 初始化状态机：AT -> AT+CWMODE=1 -> 就绪。
 * 3. 就绪后每 5 秒发一次 AT+CWLAP，扫描结果逐行解析并缓存。
 * 4. 本文件定义了 USART2_IRQHandler；若以后在 CubeMX 中勾选 USART2 NVIC，
 *    请把该中断函数移到 stm32f1xx_it.c，避免重复定义。
 */

#include "esp8266.h"
#include <stdlib.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

typedef enum
{
    ESP_INIT_OFF = 0,
    ESP_INIT_WAIT_AT,
    ESP_INIT_WAIT_MODE,
    ESP_INIT_READY
} ESP_InitState_t;

static uint8_t  s_rx_buf[ESP8266_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t  s_rx_byte;
static char     s_line[ESP8266_LINE_BUFFER_SIZE];
static uint16_t s_line_len = 0U;
static uint8_t  s_line_drop = 0U;

static ESP8266_APInfo_t s_ap_list[ESP8266_MAX_AP];
static uint16_t s_ap_count = 0U;

static ESP_InitState_t s_init_state = ESP_INIT_OFF;
static uint32_t s_cmd_tick = 0U;
static uint32_t s_retry_tick = 0U;
static uint32_t s_scan_tick = 0U;
static uint32_t s_last_scan_tick = 0U;
static uint8_t  s_scanning = 0U;
static uint8_t  s_error = 0U;
static uint8_t  s_target_found = 0U;
static uint8_t  s_connecting = 0U;
static uint8_t  s_connected = 0U;
static uint8_t  s_connect_error = 0U;
static uint32_t s_connect_tick = 0U;
static uint8_t (*s_line_hook)(const char *line) = NULL;

static void esp_send(const char *cmd)
{
    uint8_t buf[64];
    uint16_t len = 0U;

    while ((cmd[len] != '\0') && (len < (sizeof(buf) - 2U)))
    {
        buf[len] = (uint8_t)cmd[len];
        len++;
    }
    buf[len++] = '\r';
    buf[len++] = '\n';
    HAL_UART_Transmit(&huart2, buf, len, 100U);
}

static void esp_sort_ap_list(void)
{
    int16_t i;
    int16_t j;
    ESP8266_APInfo_t key;

    for (i = 1; i < (int16_t)s_ap_count; i++)
    {
        key = s_ap_list[i];
        j = i - 1;
        while ((j >= 0) && (s_ap_list[j].rssi < key.rssi))
        {
            s_ap_list[j + 1] = s_ap_list[j];
            j--;
        }
        s_ap_list[j + 1] = key;
    }
}

static void esp_parse_cwlap(char *line)
{
    char *p;
    char *q;
    ESP8266_APInfo_t ap;
    int ecn;
    int rssi;
    int channel = 0;
    int ssid_len;

    p = strchr(line, '(');
    if (p == NULL)
    {
        return;
    }
    p++;

    memset(&ap, 0, sizeof(ap));

    ecn = atoi(p);
    p = strchr(p, ',');
    if (p == NULL)
    {
        return;
    }
    p++;

    if (*p == '"')
    {
        p++;
        q = strchr(p, '"');
        if (q == NULL)
        {
            return;
        }
        ssid_len = (int)(q - p);
        if (ssid_len > (int)(ESP8266_SSID_MAX - 1U))
        {
            ssid_len = (int)(ESP8266_SSID_MAX - 1U);
        }
        memcpy(ap.ssid, p, (size_t)ssid_len);
        ap.ssid[ssid_len] = '\0';
        p = q + 1;
    }
    else
    {
        return;
    }

    p = strchr(p, ',');
    if (p == NULL)
    {
        return;
    }
    p++;
    rssi = atoi(p);
    ap.rssi = (int8_t)rssi;
    ap.ecn = (uint8_t)ecn;

    p = strchr(p, ',');
    if (p != NULL)
    {
        p++;
        if (*p == '"')
        {
            p++;
            q = strchr(p, '"');
            if (q != NULL)
            {
                int mac_len = (int)(q - p);
                if (mac_len > (int)(ESP8266_MAC_MAX - 1U))
                {
                    mac_len = (int)(ESP8266_MAC_MAX - 1U);
                }
                memcpy(ap.mac, p, (size_t)mac_len);
                ap.mac[mac_len] = '\0';
                p = q + 1;
            }
        }
    }

    if (p != NULL)
    {
        p = strchr(p, ',');
        if (p != NULL)
        {
            channel = atoi(p + 1);
        }
    }
    if (channel < 0)
    {
        channel = 0;
    }
    if (channel > 255)
    {
        channel = 255;
    }
    ap.channel = (uint8_t)channel;

    if (s_ap_count < ESP8266_MAX_AP)
    {
        s_ap_list[s_ap_count] = ap;
        s_ap_count++;
    }

    if (strcmp(ap.ssid, ESP8266_TARGET_SSID) == 0)
    {
        s_target_found = 1U;
    }
}

static void esp_connect_target(void)
{
    if ((s_connecting != 0U) || (s_connected != 0U))
    {
        return;
    }

    s_scanning = 0U;
    s_connecting = 1U;
    s_connect_error = 0U;
    s_connect_tick = HAL_GetTick();
    esp_send("AT+CWJAP=\"" ESP8266_TARGET_SSID "\",\"" ESP8266_TARGET_PASS "\"");
}

static void esp_on_ok(void)
{
    switch (s_init_state)
    {
        case ESP_INIT_WAIT_AT:
            s_init_state = ESP_INIT_WAIT_MODE;
            s_cmd_tick = HAL_GetTick();
            esp_send("AT+CWMODE=1");
            break;

        case ESP_INIT_WAIT_MODE:
            s_init_state = ESP_INIT_READY;
            s_last_scan_tick = HAL_GetTick() - ESP8266_SCAN_INTERVAL_MS;
            break;

        default:
            if (s_connecting != 0U)
            {
                s_connecting = 0U;
                s_connected = 1U;
                s_connect_error = 0U;
            }
            else if (s_scanning != 0U)
            {
                s_scanning = 0U;
                esp_sort_ap_list();
                s_last_scan_tick = HAL_GetTick();

                if ((s_target_found != 0U) && (s_connected == 0U))
                {
                    esp_connect_target();
                }
            }
            break;
    }
}

static void esp_on_error(void)
{
    if (s_connecting != 0U)
    {
        s_connecting = 0U;
        s_connected = 0U;
        s_connect_error = 1U;
        s_target_found = 0U;
        s_last_scan_tick = HAL_GetTick();
    }
    else if (s_init_state != ESP_INIT_READY)
    {
        s_init_state = ESP_INIT_OFF;
        s_retry_tick = HAL_GetTick();
    }
    else if (s_scanning != 0U)
    {
        s_scanning = 0U;
        s_error = 1U;
        s_last_scan_tick = HAL_GetTick();
    }
}

static void esp_handle_line(char *line)
{
    while ((*line == ' ') || (*line == '\t'))
    {
        line++;
    }

    if (strstr(line, "WIFI GOT IP") != NULL)
    {
        if ((s_connecting != 0U) || (s_connected != 0U))
        {
            s_connecting = 0U;
            s_connected = 1U;
            s_connect_error = 0U;
        }
        return;
    }
    if (strstr(line, "WIFI DISCONNECT") != NULL)
    {
        if (s_connected != 0U)
        {
            s_connected = 0U;
            s_connecting = 0U;
            s_target_found = 0U;
            s_last_scan_tick = HAL_GetTick();
        }
        return;
    }
    if (strncmp(line, "+CWLAP:", 7U) == 0)
    {
        esp_parse_cwlap(line + 7);
        return;
    }
    if (strcmp(line, "OK") == 0)
    {
        esp_on_ok();
        return;
    }
    if ((strcmp(line, "ERROR") == 0) || (strcmp(line, "FAIL") == 0))
    {
        esp_on_error();
        return;
    }
    if (strstr(line, "no change") != NULL)
    {
        esp_on_ok();
        return;
    }
    /* 其他行（ready、WIFI CONNECTED、回显等）忽略 */
}

void ESP8266_Init(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_line_len = 0U;
    s_line_drop = 0U;
    s_ap_count = 0U;
    s_scanning = 0U;
    s_error = 0U;
    s_target_found = 0U;
    s_connecting = 0U;
    s_connected = 0U;
    s_connect_error = 0U;

    HAL_NVIC_SetPriority(USART2_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);

    s_init_state = ESP_INIT_WAIT_AT;
    s_cmd_tick = HAL_GetTick();
    s_retry_tick = s_cmd_tick;
    esp_send("AT");
}

void ESP8266_StartScan(void)
{
    if (s_init_state != ESP_INIT_READY)
    {
        return;
    }
    if ((s_connecting != 0U) || (s_connected != 0U))
    {
        return;
    }

    s_ap_count = 0U;
    s_error = 0U;
    s_connect_error = 0U;
    s_target_found = 0U;
    s_scanning = 1U;
    s_scan_tick = HAL_GetTick();
    esp_send("AT+CWLAP");
}

void ESP8266_Process(void)
{
    uint32_t now;

    while (s_rx_tail != s_rx_head)
    {
        uint8_t c = s_rx_buf[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % ESP8266_RX_BUFFER_SIZE);

        if (c == '\n')
        {
            if (s_line_drop == 0U)
            {
                s_line[s_line_len] = '\0';
                if ((s_line_hook == NULL) || (s_line_hook(s_line) == 0U))
                {
                    esp_handle_line(s_line);
                }
            }
            s_line_len = 0U;
            s_line_drop = 0U;
        }
        else if (c != '\r')
        {
            if (s_line_len < (ESP8266_LINE_BUFFER_SIZE - 1U))
            {
                s_line[s_line_len] = (char)c;
                s_line_len++;
            }
            else
            {
                s_line_drop = 1U;
            }
        }
    }

    now = HAL_GetTick();
    switch (s_init_state)
    {
        case ESP_INIT_WAIT_AT:
        case ESP_INIT_WAIT_MODE:
            if ((now - s_cmd_tick) > ESP8266_AT_TIMEOUT_MS)
            {
                s_init_state = ESP_INIT_OFF;
                s_retry_tick = now;
            }
            break;

        case ESP_INIT_OFF:
            if ((now - s_retry_tick) > ESP8266_RETRY_INTERVAL_MS)
            {
                s_init_state = ESP_INIT_WAIT_AT;
                s_cmd_tick = now;
                s_retry_tick = now;
                esp_send("AT");
            }
            break;

        case ESP_INIT_READY:
            if (s_connecting != 0U)
            {
                if ((now - s_connect_tick) > ESP8266_CONNECT_TIMEOUT_MS)
                {
                    s_connecting = 0U;
                    s_connected = 0U;
                    s_connect_error = 1U;
                    s_target_found = 0U;
                    s_last_scan_tick = now;
                }
            }
            else if (s_connected != 0U)
            {
                /* 已连上目标 WiFi，停止周期扫描，避免影响连接 */
            }
            else if (s_scanning != 0U)
            {
                if ((now - s_scan_tick) > ESP8266_SCAN_TIMEOUT_MS)
                {
                    s_scanning = 0U;
                    s_error = 1U;
                    s_last_scan_tick = now;
                }
            }
            else if ((now - s_last_scan_tick) >= ESP8266_SCAN_INTERVAL_MS)
            {
                ESP8266_StartScan();
            }
            break;

        default:
            break;
    }
}

uint8_t ESP8266_IsReady(void)
{
    return (s_init_state == ESP_INIT_READY) ? 1U : 0U;
}

uint8_t ESP8266_IsScanning(void)
{
    return s_scanning;
}

uint8_t ESP8266_IsConnecting(void)
{
    return s_connecting;
}

uint8_t ESP8266_IsConnected(void)
{
    return s_connected;
}

uint8_t ESP8266_HasConnectError(void)
{
    return s_connect_error;
}

uint8_t ESP8266_HasError(void)
{
    return s_error;
}

uint16_t ESP8266_GetAPCount(void)
{
    return s_ap_count;
}

const ESP8266_APInfo_t *ESP8266_GetAP(uint16_t index)
{
    if (index >= s_ap_count)
    {
        return NULL;
    }
    return &s_ap_list[index];
}

void ESP8266_SendRaw(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return;
    }
    HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 300U);
}

void ESP8266_SetLineHook(uint8_t (*hook)(const char *line))
{
    s_line_hook = hook;
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        uint16_t next = (uint16_t)((s_rx_head + 1U) % ESP8266_RX_BUFFER_SIZE);
        if (next != s_rx_tail)
        {
            s_rx_buf[s_rx_head] = s_rx_byte;
            s_rx_head = next;
        }
        (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);
    }
}
