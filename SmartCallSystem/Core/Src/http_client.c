/**
 * @file    http_client.c
 * @brief   ESP8266 AT 指令实现的非阻塞 HTTP 上报
 *
 * 流程：
 *   AT+CIPSTART="TCP","IP",PORT  →  等 OK
 *   AT+CIPSEND=<长度>            →  延时 120ms 后写入 HTTP 报文
 *   等 SEND OK / CLOSED          →  AT+CIPCLOSE 收尾
 *
 * 所有等待都有超时，任何异常都不会卡住主循环。
 * 报文内容由本地队列状态自动生成，因此 ui_main 无需感知本模块。
 */

#include "http_client.h"
#include "app_queue.h"
#include "esp8266.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_CONNECT_TIMEOUT_MS  3000U
#define HTTP_SEND_DELAY_MS       120U
#define HTTP_DATA_TIMEOUT_MS     3000U
#define HTTP_CLOSE_TIMEOUT_MS    1000U
#define HTTP_PULL_INTERVAL_MS    5000U

#define HTTP_REQUEST_SIZE        320U
#define HTTP_JSON_SIZE           176U
#define HTTP_DEBUG_QUEUE_SIZE    16U

typedef enum
{
    HTTP_IDLE = 0,
    HTTP_CIPSTART,
    HTTP_CIPSEND,
    HTTP_DATA,
    HTTP_CLOSE
} HTTP_State_t;

#define HTTP_METHOD_POST  0U
#define HTTP_METHOD_GET   1U

static HTTP_State_t s_state = HTTP_IDLE;
static uint32_t     s_tick = 0U;
static uint32_t     s_hb_tick = 0U;
static uint32_t     s_pull_tick = 0U;
static uint32_t     s_last_ver = 0U;

static char     s_json[HTTP_JSON_SIZE];
static char     s_path[64];
static char     s_req[HTTP_REQUEST_SIZE];
static uint16_t s_req_len = 0U;

static uint8_t  s_pending = 0U;
static uint8_t  s_pending_prio = 0U;   /* 1 = 叫号事件，优先于心跳/拉取 */
static uint8_t  s_method = HTTP_METHOD_POST;
static uint8_t  s_expect_body = 0U;     /* GET 请求需要解析响应体 */
static uint8_t  s_in_body = 0U;         /* 已越过 HTTP header 空行 */
static uint8_t  s_ok = 0U;
static uint8_t  s_err = 0U;

/* 按键调试快照独立环形队列，避免被叫号/心跳报文覆盖 */
static char     s_debug_json[HTTP_DEBUG_QUEUE_SIZE][HTTP_JSON_SIZE];
static uint8_t  s_debug_head = 0U;
static uint8_t  s_debug_tail = 0U;
static uint8_t  s_debug_count = 0U;

/* GET 拉取类型：0 = 等候号列表(pending)，1 = 诊室状态(queue) */
static uint8_t  s_pull_type = 0U;
static uint32_t s_queue_tick = 0U;      /* 拉取诊室状态的计时 */

/* 正在接收的请求属性。请求发出后单独保存，
   防止后续排队请求覆盖 s_expect_body/s_pull_type，导致响应体解析错误。 */
static uint8_t  s_rx_expect_body = 0U;
static uint8_t  s_rx_pull_type = 0U;

/* 服务端同步回来的显示数据（供 ui_main 直接读取） */
uint16_t HTTP_SrvWaiting = 0U;
uint16_t HTTP_SrvPassed  = 0U;
uint8_t  HTTP_SrvOnline  = 0U;


/* --------------------------------------------------------------------------
 * AT 指令行回调
 * -------------------------------------------------------------------------- */
static void http_parse_tickets(const char *line);
static void http_parse_queue(const char *line);

static uint8_t http_line_hook(const char *line)
{
    if (s_state == HTTP_IDLE)
    {
        return 0U;
    }

    if (strstr(line, "SEND OK") != NULL)
    {
        if (s_rx_expect_body != 0U)
        {
            /* GET：请求已发出，继续等待响应体，重置超时 */
            s_tick = HAL_GetTick();
        }
        else
        {
            s_ok = 1U;
        }
        return 1U;
    }
    if (strstr(line, "SEND FAIL") != NULL)
    {
        s_err = 1U;
        return 1U;
    }
    if (strstr(line, "ALREADY CONNECTED") != NULL)
    {
        /* 不复用状态未知的旧连接，交给 CIPSTART 失败分支统一关闭重连。 */
        s_err = 1U;
        return 1U;
    }
    if (strstr(line, "link is not valid") != NULL)
    {
        s_err = 1U;
        return 1U;
    }
    if (strcmp(line, "OK") == 0)
    {
        s_ok = 1U;
        return 1U;
    }
    if (strcmp(line, "ERROR") == 0)
    {
        s_err = 1U;
        return 1U;
    }

    /* GET 请求：优先解析响应体，再判断是否包含 CLOSED。
       因为响应体若末尾无换行，会与 CLOSED 合并为一行，
       必须先解析 body 再处理 CLOSED。 */
    if ((s_state == HTTP_DATA) && (s_rx_expect_body != 0U))
    {
        if (s_in_body != 0U)
        {
            if (s_rx_pull_type == 0U)
            {
                http_parse_tickets(line);
            }
            else
            {
                http_parse_queue(line);
            }
        }
        else if (line[0] == '\0')
        {
            /* 空行表示 header 结束，之后是响应体 */
            s_in_body = 1U;
        }
        if (strstr(line, "CLOSED") != NULL)
        {
            s_ok = 1U;
        }
        return 1U;
    }

    /* 非 GET 请求：CLOSED 正常处理 */
    if (strstr(line, "CLOSED") != NULL)
    {
        if (s_state == HTTP_DATA)
        {
            s_ok = 1U;
        }
        else
        {
            s_err = 1U;
        }
        return 1U;
    }

    return 0U;
}


/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */
static void http_send_cmd(const char *cmd)
{
    char buf[96];
    int  n = snprintf(buf, sizeof(buf), "%s\r\n", cmd);

    if (n > 0)
    {
        ESP8266_SendRaw((uint8_t *)buf, (uint16_t)n);
    }
}

static uint8_t http_build(void)
{
    int n;

    if (s_method == HTTP_METHOD_GET)
    {
        n = snprintf(s_req, sizeof(s_req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     s_path, HTTP_SERVER_HOST, (unsigned)HTTP_SERVER_PORT);
    }
    else
    {
        uint16_t json_len = (uint16_t)strlen(s_json);
        n = snprintf(s_req, sizeof(s_req),
                     "POST %s HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     s_path, HTTP_SERVER_HOST, (unsigned)HTTP_SERVER_PORT,
                     (unsigned)json_len, s_json);
    }

    if ((n <= 0) || ((uint32_t)n >= sizeof(s_req)))
    {
        s_req_len = 0U;
        return 0U;
    }

    s_req_len = (uint16_t)n;
    return 1U;
}

static void http_queue(const char *path, uint8_t prio, uint8_t method)
{
    /* 已有叫号事件排队时，心跳/拉取不覆盖它 */
    if ((s_pending != 0U) && (s_pending_prio != 0U) && (prio == 0U))
    {
        return;
    }

    strncpy(s_path, path, sizeof(s_path) - 1U);
    s_path[sizeof(s_path) - 1U] = '\0';
    s_pending       = 1U;
    s_pending_prio  = prio;
    s_method        = method;
    s_expect_body   = (method == HTTP_METHOD_GET) ? 1U : 0U;
}

/* 解析逗号分隔的等候号，例如 "A013,A014,A015"，追加到本地队列 */
static void http_parse_tickets(const char *line)
{
    char buf[96];
    char *token;
    uint16_t seq;
    uint8_t prefix;

    strncpy(buf, line, sizeof(buf) - 1U);
    buf[sizeof(buf) - 1U] = '\0';

    token = strtok(buf, ",");
    while (token != NULL)
    {
        if ((token[0] >= 'A') && (token[0] <= 'Z'))
        {
            prefix = (uint8_t)token[0];
            seq = (uint16_t)atoi(token + 1);
            if (seq > 0U)
            {
                (void)QUEUE_Append(prefix, seq);
            }
        }
        token = strtok(NULL, ",");
    }
}

/* 解析 /api/queue 返回的 JSON，提取 waiting/passed/online */
static void http_parse_queue(const char *line)
{
    const char *p;
    int         val;

    p = strstr(line, "\"waiting\":");
    if (p != NULL)
    {
        val = atoi(p + 10);
        if (val >= 0)
        {
            HTTP_SrvWaiting = (uint16_t)val;
        }
    }

    p = strstr(line, "\"passed\":");
    if (p != NULL)
    {
        val = atoi(p + 9);
        if (val >= 0)
        {
            HTTP_SrvPassed = (uint16_t)val;
        }
    }

    if (strstr(line, "\"online\":true") != NULL)
    {
        HTTP_SrvOnline = 1U;
    }
    else if (strstr(line, "\"online\":false") != NULL)
    {
        HTTP_SrvOnline = 0U;
    }
}

/* 用当前队列状态生成叫号/心跳报文 */
static void http_post_current(uint8_t prio)
{
    const Ticket_t *cur = QUEUE_GetCurrent();
    char code[8];

    if (cur != NULL)
    {
        QUEUE_FormatCode(cur, code);
    }
    else
    {
        strcpy(code, "----");
    }

    (void)snprintf(s_json, sizeof(s_json),
                   "{\"room\":\"" HTTP_ROOM "\",\"ticket\":\"%s\",\"waiting\":%u,"
                   "\"passed\":%u,\"paused\":%s,\"state\":\"%s\"}",
                   code,
                   (unsigned)QUEUE_GetWaitCount(),
                   (unsigned)QUEUE_GetPassedCount(),
                   (QUEUE_IsPaused() != 0U) ? "true" : "false",
                   (QUEUE_IsPaused() != 0U) ? "paused" : "call");

    http_queue((prio != 0U) ? "/api/device/call" : "/api/device/heartbeat",
               prio, HTTP_METHOD_POST);
}

/* 拉取网页端新取的等候号（since = 本地最大序号） */
static void http_pull_pending(void)
{
    uint16_t max_seq = QUEUE_GetMaxSeq();

    (void)snprintf(s_path, sizeof(s_path),
                   "/api/device/pending?room=" HTTP_ROOM "&since=%u",
                   (unsigned)max_seq);
    s_pending       = 1U;
    s_pending_prio  = 0U;
    s_method        = HTTP_METHOD_GET;
    s_expect_body   = 1U;
    s_pull_type     = 0U;
}

/* 拉取诊室状态（等候/过号/在线），以服务端数据为准 */
static void http_pull_queue(void)
{
    (void)snprintf(s_path, sizeof(s_path),
                   "/api/queue?room=" HTTP_ROOM);
    s_pending       = 1U;
    s_pending_prio  = 0U;
    s_method        = HTTP_METHOD_GET;
    s_expect_body   = 1U;
    s_pull_type     = 1U;
}

static void http_begin_close(uint32_t now)
{
    http_send_cmd("AT+CIPCLOSE");
    s_state = HTTP_CLOSE;
    s_tick  = now;
    s_ok    = 0U;
    s_err   = 0U;
}


/* --------------------------------------------------------------------------
 * 对外接口
 * -------------------------------------------------------------------------- */
void HTTP_Init(void)
{
    s_state        = HTTP_IDLE;
    s_tick         = HAL_GetTick();
    s_hb_tick      = HAL_GetTick();
    s_pull_tick    = HAL_GetTick();
    s_queue_tick   = HAL_GetTick() - HTTP_PULL_INTERVAL_MS;  /* 开机立即拉一次 */
    s_last_ver     = QUEUE_GetVersion();
    s_req_len      = 0U;
    s_pending      = 0U;
    s_pending_prio = 0U;
    s_method       = HTTP_METHOD_POST;
    s_expect_body  = 0U;
    s_in_body      = 0U;
    s_ok           = 0U;
    s_err          = 0U;
    s_debug_head   = 0U;
    s_debug_tail   = 0U;
    s_debug_count  = 0U;
    s_pull_type    = 0U;
    s_rx_expect_body = 0U;
    s_rx_pull_type = 0U;
    s_path[0]      = '\0';
    s_json[0]      = '\0';

    HTTP_SrvWaiting = 0U;
    HTTP_SrvPassed  = 0U;
    HTTP_SrvOnline  = 0U;

    ESP8266_SetLineHook(http_line_hook);
}

void HTTP_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t ver = QUEUE_GetVersion();

    /* WiFi 断开时立即复位 HTTP 状态机，避免 AT+CIPSTART 等指令
       与 ESP8266 重连扫描(AT+CWLAP)/连接(AT+CWJAP)产生串口冲突。 */
    if (ESP8266_IsConnected() == 0U)
    {
        if (s_state != HTTP_IDLE)
        {
            s_state  = HTTP_IDLE;
            s_ok     = 0U;
            s_err    = 0U;
        }
        s_pending      = 0U;
        s_pending_prio = 0U;
        s_hb_tick      = now;   /* 重连后立即发心跳 */
        s_pull_tick    = now;
        s_queue_tick   = now - HTTP_PULL_INTERVAL_MS;  /* 重连后立即拉诊室状态 */
        return;
    }

    /* 队列变化（叫号/过号/暂停）优先上报 */
    if (ver != s_last_ver)
    {
        s_last_ver = ver;
        http_post_current(1U);
        s_hb_tick = now;
    }

    /* 拉取优先于心跳。请求槽被占用时保持到期状态，下一轮补排；
       只有真正入队才重置计时，避免心跳与拉取同周期时拉取被永久跳过。 */
    if (((now - s_pull_tick) >= HTTP_PULL_INTERVAL_MS) &&
        (s_pending == 0U))
    {
        http_pull_pending();
        s_pull_tick = now;
    }

    if (((now - s_queue_tick) >= HTTP_PULL_INTERVAL_MS) &&
        (s_pending == 0U))
    {
        http_pull_queue();
        s_queue_tick = now;
    }

    /* 心跳最后排队，绝不覆盖已等待的叫号或拉取请求。 */
    if (((now - s_hb_tick) >= HTTP_HEARTBEAT_MS) &&
        (s_pending == 0U))
    {
        s_hb_tick = now;
        http_post_current(0U);
    }

    switch (s_state)
    {
        case HTTP_IDLE:
            /* 无业务请求时再取一条按键快照，取出后立即构造请求，
               不会在等待期间被其他报文覆盖。 */
            if ((s_pending == 0U) && (s_debug_count != 0U))
            {
                strcpy(s_json, s_debug_json[s_debug_tail]);
                s_debug_tail = (uint8_t)((s_debug_tail + 1U) % HTTP_DEBUG_QUEUE_SIZE);
                s_debug_count--;
                http_queue("/api/device/debug", 0U, HTTP_METHOD_POST);
            }
            if ((s_pending == 0U) || (ESP8266_IsConnected() == 0U))
            {
                break;
            }
            if (http_build() == 0U)
            {
                s_pending = 0U;
                break;
            }
            /* 请求开始发送后锁定接收解析属性，之后允许排队下一请求。 */
            s_rx_expect_body = s_expect_body;
            s_rx_pull_type   = s_pull_type;
            {
                char cmd[64];
                (void)snprintf(cmd, sizeof(cmd),
                               "AT+CIPSTART=\"TCP\",\"%s\",%u",
                               HTTP_SERVER_HOST, (unsigned)HTTP_SERVER_PORT);
                http_send_cmd(cmd);
            }
            s_pending      = 0U;
            s_pending_prio = 0U;
            s_in_body      = 0U;
            s_ok           = 0U;
            s_err          = 0U;
            s_state        = HTTP_CIPSTART;
            s_tick         = now;
            break;

        case HTTP_CIPSTART:
            if (s_ok != 0U)
            {
                char cmd[32];
                (void)snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u",
                               (unsigned)s_req_len);
                http_send_cmd(cmd);
                s_ok    = 0U;
                s_state = HTTP_CIPSEND;
                s_tick  = now;
            }
            else if ((s_err != 0U) || ((now - s_tick) > HTTP_CONNECT_TIMEOUT_MS))
            {
                /* CIPSTART 失败常由残留 TCP 连接导致。
                   必须执行 CIPCLOSE 清理，否则后续每次 CIPSTART 都会 ERROR。 */
                http_begin_close(now);
            }
            break;

        case HTTP_CIPSEND:
            /* 模块回 "> " 不一定带换行，直接按固定延时推进更可靠 */
            if ((now - s_tick) >= HTTP_SEND_DELAY_MS)
            {
                ESP8266_SendRaw((uint8_t *)s_req, s_req_len);
                s_ok    = 0U;
                s_err   = 0U;
                s_state = HTTP_DATA;
                s_tick  = now;
            }
            break;

        case HTTP_DATA:
            /* POST：SEND OK 即可关闭；GET：需等响应体收完（CLOSED）或超时 */
            if ((s_ok != 0U) || (s_err != 0U) ||
                ((now - s_tick) > HTTP_DATA_TIMEOUT_MS))
            {
                http_begin_close(now);
            }
            break;

        case HTTP_CLOSE:
            if ((s_ok != 0U) || (s_err != 0U) ||
                ((now - s_tick) > HTTP_CLOSE_TIMEOUT_MS))
            {
                s_ok    = 0U;
                s_err   = 0U;
                s_state = HTTP_IDLE;
            }
            break;

        default:
            s_state = HTTP_IDLE;
            break;
    }
}

uint8_t HTTP_IsBusy(void)
{
    return (s_state != HTTP_IDLE) ? 1U : 0U;
}

/* 按键调试上报：快照先写入独立队列，业务空闲时逐条发送。
   即使 WiFi 暂时断开也保留按键记录；队列满时丢弃最旧的一条。 */
void HTTP_PostDebug(const char *key)
{
    const Ticket_t *cur;
    char code[8];

    cur = QUEUE_GetCurrent();
    if (cur != NULL)
    {
        QUEUE_FormatCode(cur, code);
    }
    else
    {
        strcpy(code, "----");
    }

    if (s_debug_count >= HTTP_DEBUG_QUEUE_SIZE)
    {
        s_debug_tail = (uint8_t)((s_debug_tail + 1U) % HTTP_DEBUG_QUEUE_SIZE);
    }
    else
    {
        s_debug_count++;
    }

    (void)snprintf(s_debug_json[s_debug_head],
                   sizeof(s_debug_json[s_debug_head]),
                   "{\"room\":\"" HTTP_ROOM "\",\"key\":\"%s\",\"cur\":\"%s\","
                   "\"wait\":%u,\"passed\":%u,\"count\":%u,\"max_seq\":%u,"
                   "\"http_state\":%u,\"wifi\":%u,"
                   "\"srv_wait\":%u,\"srv_pass\":%u,\"srv_online\":%u}",
                   key, code,
                   (unsigned)QUEUE_GetWaitCount(),
                   (unsigned)QUEUE_GetPassedCount(),
                   (unsigned)QUEUE_GetCount(),
                   (unsigned)QUEUE_GetMaxSeq(),
                   (unsigned)s_state,
                   (unsigned)ESP8266_IsConnected(),
                   (unsigned)HTTP_SrvWaiting,
                   (unsigned)HTTP_SrvPassed,
                   (unsigned)HTTP_SrvOnline);

    s_debug_head = (uint8_t)((s_debug_head + 1U) % HTTP_DEBUG_QUEUE_SIZE);
}
