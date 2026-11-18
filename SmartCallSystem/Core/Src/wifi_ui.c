/**
 * @file    wifi_ui.c
 * @brief   WiFi 扫描结果屏幕显示（ST7735 128x160，8x16 ASCII）
 *
 * 布局：
 *   y=0   标题 WIFI SCAN
 *   y=18  状态行（ESP 未响应 / 扫描中 / 扫描失败 / 找到 N 个）
 *   y=36  7 行 AP 列表，每行 17 像素
 *          左侧 SSID（最多 11 个字符），右侧 RSSI（x=96）
 */

#include "wifi_ui.h"
#include "esp8266.h"
#include "st7735.h"
#include <stdio.h>
#include <string.h>

#define UI_STATUS_Y       18U
#define UI_LIST_Y         36U
#define UI_ROW_H          17U
#define UI_ROWS           7U
#define UI_SSID_CHARS     11U
#define UI_RSSI_X         96U

static uint8_t  s_last_ready = 0xFFU;
static uint8_t  s_last_scanning = 0xFFU;
static uint8_t  s_last_connecting = 0xFFU;
static uint8_t  s_last_connected = 0xFFU;
static uint8_t  s_last_connect_error = 0xFFU;
static uint8_t  s_last_error = 0xFFU;
static uint16_t s_last_count = 0xFFFFU;
static uint32_t s_last_status_tick = 0U;
static uint32_t s_last_list_tick = 0U;

static void ui_draw_status(void)
{
    char text[24];
    uint16_t color = ST7735_WHITE;

    ST7735_FillRect(0U, UI_STATUS_Y, ST7735_WIDTH, 17U, ST7735_BLACK);

    if (ESP8266_IsReady() == 0U)
    {
        strcpy(text, "ESP NO REPLY");
        color = ST7735_YELLOW;
    }
    else if (ESP8266_IsConnected() != 0U)
    {
        strcpy(text, "CONNECTED");
        color = ST7735_GREEN;
    }
    else if (ESP8266_IsConnecting() != 0U)
    {
        strcpy(text, "CONNECTING...");
        color = ST7735_YELLOW;
    }
    else if (ESP8266_IsScanning() != 0U)
    {
        (void)snprintf(text, sizeof(text), "SCANNING... %u",
                       (unsigned int)ESP8266_GetAPCount());
        color = ST7735_YELLOW;
    }
    else if (ESP8266_HasConnectError() != 0U)
    {
        strcpy(text, "CONNECT FAIL");
        color = ST7735_RED;
    }
    else if (ESP8266_HasError() != 0U)
    {
        strcpy(text, "SCAN ERROR");
        color = ST7735_RED;
    }
    else
    {
        (void)snprintf(text, sizeof(text), "FOUND %u AP",
                       (unsigned int)ESP8266_GetAPCount());
        color = ST7735_GREEN;
    }

    ST7735_DrawString(4U, UI_STATUS_Y, text, color, ST7735_BLACK, 1U);
}

static void ui_make_name(const ESP8266_APInfo_t *ap, char *out, uint8_t max_chars)
{
    uint8_t i;
    uint8_t n = 0U;
    uint8_t non_ascii = 0U;

    for (i = 0U; ap->ssid[i] != '\0'; i++)
    {
        uint8_t c = (uint8_t)ap->ssid[i];
        if ((c < 0x20U) || (c > 0x7EU))
        {
            non_ascii = 1U;
        }
    }

    if (non_ascii != 0U)
    {
        /* 8x16 字库只有 ASCII，中文 SSID 用 MAC 后 4 位区分 */
        if (strlen(ap->mac) >= 17U)
        {
            (void)snprintf(out, (size_t)max_chars + 1U, "CN-%c%c%c%c",
                           ap->mac[12], ap->mac[13], ap->mac[15], ap->mac[16]);
        }
        else
        {
            (void)snprintf(out, (size_t)max_chars + 1U, "CN-AP");
        }
        return;
    }

    if (ap->ssid[0] == '\0')
    {
        (void)snprintf(out, (size_t)max_chars + 1U, "(HIDDEN)");
        return;
    }

    while ((ap->ssid[n] != '\0') && (n < max_chars))
    {
        out[n] = ap->ssid[n];
        n++;
    }
    out[n] = '\0';
}

static void ui_draw_list(void)
{
    uint8_t idx[ESP8266_MAX_AP];
    uint16_t count;
    uint16_t i;
    uint16_t j;
    uint16_t tmp;

    ST7735_FillRect(0U, UI_LIST_Y, ST7735_WIDTH, (ST7735_HEIGHT - UI_LIST_Y), ST7735_BLACK);

    count = ESP8266_GetAPCount();
    if (count > ESP8266_MAX_AP)
    {
        count = ESP8266_MAX_AP;
    }

    if (count == 0U)
    {
        if ((ESP8266_IsReady() != 0U) && (ESP8266_IsScanning() == 0U) &&
            (ESP8266_HasError() == 0U))
        {
            ST7735_DrawString(4U, 70U, "NO AP FOUND", ST7735_GRAY, ST7735_BLACK, 1U);
        }
        return;
    }

    /* 按 RSSI 从强到弱排序索引，不改动驱动缓存顺序 */
    for (i = 0U; i < count; i++)
    {
        idx[i] = (uint8_t)i;
    }
    for (i = 1U; i < count; i++)
    {
        tmp = idx[i];
        j = i;
        while ((j > 0U) && (ESP8266_GetAP(idx[j - 1U])->rssi < ESP8266_GetAP(tmp)->rssi))
        {
            idx[j] = idx[j - 1U];
            j--;
        }
        idx[j] = (uint8_t)tmp;
    }

    for (i = 0U; (i < UI_ROWS) && (i < count); i++)
    {
        const ESP8266_APInfo_t *ap = ESP8266_GetAP(idx[i]);
        uint16_t y = (uint16_t)(UI_LIST_Y + (i * UI_ROW_H));
        char name[UI_SSID_CHARS + 1U];
        char rssi_text[8];
        uint16_t rssi_color;
        uint16_t ssid_color = ST7735_WHITE;

        if (ap == NULL)
        {
            break;
        }

        ui_make_name(ap, name, UI_SSID_CHARS);
        (void)snprintf(rssi_text, sizeof(rssi_text), "%d", (int)ap->rssi);

        if (ap->rssi >= -50)
        {
            rssi_color = ST7735_GREEN;
        }
        else if (ap->rssi >= -70)
        {
            rssi_color = ST7735_YELLOW;
        }
        else
        {
            rssi_color = ST7735_RED;
        }

        if ((ESP8266_IsConnected() != 0U) &&
            (strncmp(ap->ssid, ESP8266_TARGET_SSID, UI_SSID_CHARS) == 0))
        {
            ssid_color = ST7735_GREEN;
        }

        ST7735_DrawString(0U, y, name, ssid_color, ST7735_BLACK, 1U);
        ST7735_DrawString(UI_RSSI_X, y, rssi_text, rssi_color, ST7735_BLACK, 1U);
    }
}

void WiFiUI_Init(void)
{
    ST7735_FillScreen(ST7735_BLACK);
    ST7735_DrawString(4U, 0U, "WIFI SCAN", ST7735_CYAN, ST7735_BLACK, 1U);
    ST7735_FillRect(0U, 17U, ST7735_WIDTH, 1U, ST7735_CYAN);
    ST7735_FillRect(0U, 35U, ST7735_WIDTH, 1U, ST7735_GRAY);

    s_last_ready = 0xFFU;
    s_last_scanning = 0xFFU;
    s_last_connecting = 0xFFU;
    s_last_connected = 0xFFU;
    s_last_connect_error = 0xFFU;
    s_last_error = 0xFFU;
    s_last_count = 0xFFFFU;
    s_last_status_tick = 0U;
    s_last_list_tick = 0U;

    ui_draw_status();
    ui_draw_list();
}

void WiFiUI_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t ready = ESP8266_IsReady();
    uint8_t scanning = ESP8266_IsScanning();
    uint8_t connecting = ESP8266_IsConnecting();
    uint8_t connected = ESP8266_IsConnected();
    uint8_t connect_error = ESP8266_HasConnectError();
    uint8_t error = ESP8266_HasError();
    uint16_t count = ESP8266_GetAPCount();

    if ((ready != s_last_ready) || (scanning != s_last_scanning) ||
        (connecting != s_last_connecting) || (connected != s_last_connected) ||
        (connect_error != s_last_connect_error) || (error != s_last_error))
    {
        ui_draw_status();
        ui_draw_list();
        s_last_ready = ready;
        s_last_scanning = scanning;
        s_last_connecting = connecting;
        s_last_connected = connected;
        s_last_connect_error = connect_error;
        s_last_error = error;
        s_last_count = count;
        s_last_status_tick = now;
        s_last_list_tick = now;
    }
    else if ((count != s_last_count) && ((now - s_last_list_tick) > 250U))
    {
        ui_draw_status();
        ui_draw_list();
        s_last_count = count;
        s_last_status_tick = now;
        s_last_list_tick = now;
    }
    else if (((scanning != 0U) || (connecting != 0U)) &&
             ((now - s_last_status_tick) > 500U))
    {
        ui_draw_status();
        s_last_status_tick = now;
    }
}
