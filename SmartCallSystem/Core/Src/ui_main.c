/**
 * @file    ui_main.c
 * @brief   医生端主界面实现（128x160，局部刷新）
 *
 * 布局：
 *   y=0    诊室 01 + WiFi 状态（已联网 / 连接中 / 未联网），分隔线 y=17
 *   y=20   正在叫号
 *   y=38   当前号码，4 倍字号（A001 占满 128 像素宽）
 *   y=104  请到 1 诊室，分隔线 y=121
 *   y=124  等候 N / 过号 M
 *   y=144  底部按键提示
 *
 * 刷新策略：队列版本变化时整体重绘正文；号码变化时黄色闪烁 500ms。
 */

#include "ui_main.h"
#include "st7735.h"
#include "font16_cn.h"
#include "app_queue.h"
#include "app_key.h"
#include "buzzer.h"
#include "jr6001.h"
#include "esp8266.h"
#include "http_client.h"

#define UI_LINE_COLOR   0x4208U     /* 深灰分隔线 */
#define UI_FLASH_MS     500U
#define UI_FLASH_STEP   125U

static uint32_t s_drawn_ver     = 0U;
static uint8_t  s_drawn_confirm = 0xFFU;
static uint8_t  s_drawn_wifi    = 0xFFU;
static uint16_t s_drawn_srv_wait = 0xFFFFU;   /* 服务端等候数变化时触发重绘 */
static uint16_t s_drawn_srv_pass = 0xFFFFU;   /* 服务端过号数变化时触发重绘 */
static uint8_t  s_flashing      = 0U;
static uint8_t  s_flash_on      = 0U;
static uint32_t s_flash_tick    = 0U;

/* 画一个汉字，返回下一个位置 */
static uint16_t ui_cn(uint16_t x, uint16_t y, uint8_t id, uint16_t fg)
{
    CN_DrawGlyph(x, y, id, fg, ST7735_BLACK);
    return (uint16_t)(x + 16U);
}

/* 画一个 8x16 ASCII 字符，返回下一个位置 */
static uint16_t ui_char(uint16_t x, uint16_t y, char c, uint16_t fg)
{
    ST7735_DrawChar(x, y, c, fg, ST7735_BLACK, 1U);
    return (uint16_t)(x + 8U);
}

/* 0=离线 1=已联网 2=连接中 */
static uint8_t ui_wifi_state(void)
{
    if (ESP8266_IsConnected() != 0U)
    {
        return 1U;
    }
    if (ESP8266_IsConnecting() != 0U)
    {
        return 2U;
    }
    return 0U;
}

static void ui_draw_header(uint8_t wifi)
{
    uint16_t x;

    ST7735_FillRect(0U, 0U, ST7735_WIDTH, 17U, ST7735_BLACK);
    (void)ui_cn(0U, 0U, CN_ZHEN, ST7735_WHITE);
    (void)ui_cn(16U, 0U, CN_SHI, ST7735_WHITE);
    ST7735_DrawString(38U, 0U, "01", ST7735_CYAN, ST7735_BLACK, 1U);

    if (wifi == 1U)
    {
        x = 80U;
        x = ui_cn(x, 0U, CN_YI, ST7735_GREEN);
        x = ui_cn(x, 0U, CN_LIAN_NET, ST7735_GREEN);
        (void)ui_cn(x, 0U, CN_WANG, ST7735_GREEN);
    }
    else if (wifi == 2U)
    {
        x = 96U;
        x = ui_cn(x, 0U, CN_LIAN_CONN, ST7735_YELLOW);
        (void)ui_cn(x, 0U, CN_ZHONG, ST7735_YELLOW);
    }
    else
    {
        x = 80U;
        x = ui_cn(x, 0U, CN_WEI, ST7735_GRAY);
        x = ui_cn(x, 0U, CN_LIAN_NET, ST7735_GRAY);
        (void)ui_cn(x, 0U, CN_WANG, ST7735_GRAY);
    }

    ST7735_DrawLine(0, 17, (int16_t)(ST7735_WIDTH - 1U), 17, UI_LINE_COLOR);
}

static void ui_draw_number(uint16_t color)
{
    const Ticket_t *cur = QUEUE_GetCurrent();

    ST7735_FillRect(0U, 38U, ST7735_WIDTH, 64U, ST7735_BLACK);

    if (cur != NULL)
    {
        char code[5];

        QUEUE_FormatCode(cur, code);
        ST7735_DrawString(0U, 38U, code, color, ST7735_BLACK, 4U);
    }
    else
    {
        ST7735_DrawString(32U, 38U, "--", ST7735_GRAY, ST7735_BLACK, 4U);
    }
}

static void ui_put2(uint16_t x, uint16_t y, uint16_t value, uint16_t fg)
{
    char buf[2];
    uint16_t v = value;

    if (v > 99U)
    {
        v = 99U;
    }
    buf[0] = (char)('0' + (v / 10U));
    buf[1] = (char)('0' + (v % 10U));

    (void)ui_char(x, y, buf[0], fg);
    (void)ui_char((uint16_t)(x + 8U), y, buf[1], fg);
}

static void ui_draw_hint(void)
{
    uint16_t x;

    ST7735_FillRect(0U, 144U, ST7735_WIDTH, 16U, ST7735_BLACK);

    if (APPKEY_IsConfirming() != 0U)
    {
        x = 4U;
        x = ui_cn(x, 144U, CN_AN, ST7735_YELLOW);
        x = ui_char(x, 144U, '#', ST7735_YELLOW);
        x = ui_cn(x, 144U, CN_QUE, ST7735_YELLOW);
        x = ui_cn(x, 144U, CN_REN, ST7735_YELLOW);
        x = (uint16_t)(x + 8U);
        x = ui_cn(x, 144U, CN_AN, ST7735_YELLOW);
        x = ui_char(x, 144U, '*', ST7735_YELLOW);
        x = ui_cn(x, 144U, CN_QU, ST7735_YELLOW);
        (void)ui_cn(x, 144U, CN_XIAO, ST7735_YELLOW);
    }
    else if (QUEUE_IsPaused() != 0U)
    {
        x = 24U;
        x = ui_cn(x, 144U, CN_ZAN, ST7735_ORANGE);
        x = ui_cn(x, 144U, CN_TING, ST7735_ORANGE);
        x = ui_cn(x, 144U, CN_ZHONG, ST7735_ORANGE);
        x = (uint16_t)(x + 8U);
        x = ui_cn(x, 144U, CN_AN, ST7735_ORANGE);
        (void)ui_char(x, 144U, '4', ST7735_ORANGE);
    }
    else
    {
        x = 4U;
        x = ui_cn(x, 144U, CN_AN, ST7735_WHITE);
        x = ui_char(x, 144U, '1', ST7735_WHITE);
        x = ui_cn(x, 144U, CN_JIAO, ST7735_WHITE);
        x = ui_cn(x, 144U, CN_HAO, ST7735_WHITE);
        x = (uint16_t)(x + 8U);
        x = ui_cn(x, 144U, CN_AN, ST7735_WHITE);
        x = ui_char(x, 144U, '2', ST7735_WHITE);
        x = ui_cn(x, 144U, CN_HUI, ST7735_WHITE);
        (void)ui_cn(x, 144U, CN_HU, ST7735_WHITE);
    }
}

static void ui_draw_body(void)
{
    uint16_t x;
    uint16_t wait;
    uint16_t passed;

    ST7735_FillRect(0U, 20U, ST7735_WIDTH, 140U, ST7735_BLACK);

    /* 正在叫号（4 个汉字居中） */
    x = 32U;
    x = ui_cn(x, 20U, CN_ZHENG, ST7735_WHITE);
    x = ui_cn(x, 20U, CN_ZAI, ST7735_WHITE);
    x = ui_cn(x, 20U, CN_JIAO, ST7735_WHITE);
    (void)ui_cn(x, 20U, CN_HAO, ST7735_WHITE);

    /* 当前号码 */
    ui_draw_number(ST7735_WHITE);

    /* 请到 1 诊室（居中） */
    x = 24U;
    x = ui_cn(x, 104U, CN_QING, ST7735_CYAN);
    x = ui_cn(x, 104U, CN_DAO, ST7735_CYAN);
    x = (uint16_t)(x + 4U);
    x = ui_char(x, 104U, '1', ST7735_CYAN);
    x = (uint16_t)(x + 4U);
    x = ui_cn(x, 104U, CN_ZHEN, ST7735_CYAN);
    (void)ui_cn(x, 104U, CN_SHI, ST7735_CYAN);

    ST7735_DrawLine(0, 121, (int16_t)(ST7735_WIDTH - 1U), 121, UI_LINE_COLOR);

    /* 等候 / 过号：以服务端同步数据为准 */
    wait   = HTTP_SrvWaiting;
    passed = HTTP_SrvPassed;

    x = 0U;
    x = ui_cn(x, 124U, CN_DENG, ST7735_WHITE);
    x = ui_cn(x, 124U, CN_HOU, ST7735_WHITE);
    ui_put2((uint16_t)(x + 4U), 124U, wait, ST7735_YELLOW);

    x = 68U;
    x = ui_cn(x, 124U, CN_GUO, ST7735_WHITE);
    x = ui_cn(x, 124U, CN_HU, ST7735_WHITE);
    ui_put2((uint16_t)(x + 4U), 124U, passed, ST7735_ORANGE);

    ui_draw_hint();
}

void UIMain_Init(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    s_drawn_wifi     = ui_wifi_state();
    s_drawn_ver      = QUEUE_GetVersion();
    s_drawn_confirm  = APPKEY_IsConfirming();
    s_drawn_srv_wait = HTTP_SrvWaiting;
    s_drawn_srv_pass = HTTP_SrvPassed;
    s_flashing       = 0U;
    s_flash_on       = 0U;
    s_flash_tick     = HAL_GetTick();

    ui_draw_header(s_drawn_wifi);
    ui_draw_body();

    if (QUEUE_TakeAnnounce() != 0U)
    {
        const Ticket_t *cur = QUEUE_GetCurrent();

        s_flashing   = 1U;
        s_flash_on   = 1U;
        s_flash_tick = HAL_GetTick();
        ui_draw_number(ST7735_YELLOW);
        if (cur != NULL)
        {
            JR6001_AnnounceTicket(cur->prefix, cur->seq);
        }
    }
}

void UIMain_Task(void)
{
    uint32_t now  = HAL_GetTick();
    uint32_t ver;
    uint8_t  wifi;
    uint8_t  confirm;

    wifi = ui_wifi_state();
    if (wifi != s_drawn_wifi)
    {
        ui_draw_header(wifi);
        s_drawn_wifi = wifi;
    }

    ver     = QUEUE_GetVersion();
    confirm = APPKEY_IsConfirming();

    if ((ver != s_drawn_ver) || (confirm != s_drawn_confirm) ||
        (HTTP_SrvWaiting != s_drawn_srv_wait) ||
        (HTTP_SrvPassed  != s_drawn_srv_pass))
    {
        ui_draw_body();
        s_drawn_ver      = ver;
        s_drawn_confirm  = confirm;
        s_drawn_srv_wait = HTTP_SrvWaiting;
        s_drawn_srv_pass = HTTP_SrvPassed;
    }

    if (QUEUE_TakeAnnounce() != 0U)
    {
        const Ticket_t *cur = QUEUE_GetCurrent();

        s_flashing   = 1U;
        s_flash_on   = 1U;
        s_flash_tick = now;
        ui_draw_number(ST7735_YELLOW);
        if (cur != NULL)
        {
            JR6001_AnnounceTicket(cur->prefix, cur->seq);
        }
    }

    if (s_flashing != 0U)
    {
        if ((now - s_flash_tick) >= UI_FLASH_MS)
        {
            s_flashing = 0U;
            ui_draw_number(ST7735_WHITE);
        }
        else
        {
            uint8_t on = (uint8_t)(((now - s_flash_tick) / UI_FLASH_STEP) & 1U);

            if (on != s_flash_on)
            {
                s_flash_on = on;
                ui_draw_number((on != 0U) ? ST7735_YELLOW : ST7735_WHITE);
            }
        }
    }
}
