/**
 * @file    app_queue.c
 * @brief   医生端本地叫号队列实现
 *
 * 规则：
 *   1. 上电预置 12 条模拟数据（A001-A012），并自动叫出 A001。
 *   2. “下一位”：当前号置为完成，取最早的等候号变为叫号中。
 *   3. “过号”：当前号置为过号；未暂停时自动顺延下一位。
 *   4. “回呼过号”：把最近一个过号重新变为当前号（原当前号置为完成）。
 *   5. 暂停期间不叫新号、不过号顺延，但仍然可以重呼当前号。
 */

#include "app_queue.h"

#define APP_QUEUE_NONE 0xFFFFU
#define APP_QUEUE_DEMO_COUNT 12U

static Ticket_t s_tickets[APP_QUEUE_MAX];
static uint16_t s_count    = 0U;
static uint16_t s_current  = APP_QUEUE_NONE;
static uint8_t  s_paused   = 0U;
static uint32_t s_version  = 0U;
static uint8_t  s_announce = 0U;

static uint16_t queue_find_wait(void)
{
    uint16_t i;

    for (i = 0U; i < s_count; i++)
    {
        if (s_tickets[i].state == TICKET_WAIT)
        {
            return i;
        }
    }
    return APP_QUEUE_NONE;
}

void QUEUE_Init(void)
{
    uint16_t i;

    for (i = 0U; (i < APP_QUEUE_DEMO_COUNT) && (i < APP_QUEUE_MAX); i++)
    {
        s_tickets[i].prefix = (uint8_t)'A';
        s_tickets[i].seq    = (uint16_t)(i + 1U);
        s_tickets[i].state  = TICKET_WAIT;
    }
    s_count    = i;
    s_current  = APP_QUEUE_NONE;
    s_paused   = 0U;
    s_version  = 1U;
    s_announce = 0U;

    /* 上电直接进入“正在叫号”状态，方便演示 */
    s_current = queue_find_wait();
    if (s_current != APP_QUEUE_NONE)
    {
        s_tickets[s_current].state = TICKET_CALLING;
    }
    s_announce = 1U;
}

uint8_t QUEUE_Next(void)
{
    uint16_t next;

    if (s_paused != 0U)
    {
        return 0U;
    }

    next = queue_find_wait();
    if (next == APP_QUEUE_NONE)
    {
        return 0U;
    }

    if (s_current != APP_QUEUE_NONE)
    {
        s_tickets[s_current].state = TICKET_DONE;
    }
    s_current = next;
    s_tickets[next].state = TICKET_CALLING;
    s_version++;
    s_announce = 1U;
    return 1U;
}

uint8_t QUEUE_Recall(void)
{
    if (s_current == APP_QUEUE_NONE)
    {
        return 0U;
    }
    s_announce = 1U;
    return 1U;
}

uint8_t QUEUE_Pass(void)
{
    uint16_t next;

    if (s_current == APP_QUEUE_NONE)
    {
        return 0U;
    }

    s_tickets[s_current].state = TICKET_PASSED;
    s_current = APP_QUEUE_NONE;
    s_version++;

    if (s_paused == 0U)
    {
        next = queue_find_wait();
        if (next != APP_QUEUE_NONE)
        {
            s_current = next;
            s_tickets[next].state = TICKET_CALLING;
            s_announce = 1U;
        }
    }
    return 1U;
}

uint8_t QUEUE_RecallPassed(void)
{
    uint16_t i      = s_count;
    uint16_t target = APP_QUEUE_NONE;

    while (i > 0U)
    {
        i--;
        if (s_tickets[i].state == TICKET_PASSED)
        {
            target = i;
            break;
        }
    }
    if (target == APP_QUEUE_NONE)
    {
        return 0U;
    }

    if (s_current != APP_QUEUE_NONE)
    {
        s_tickets[s_current].state = TICKET_DONE;
    }
    s_tickets[target].state = TICKET_CALLING;
    s_current  = target;
    s_version++;
    s_announce = 1U;
    return 1U;
}

uint8_t QUEUE_TogglePause(void)
{
    s_paused = (uint8_t)((s_paused == 0U) ? 1U : 0U);
    s_version++;
    return s_paused;
}

uint8_t QUEUE_IsPaused(void)
{
    return s_paused;
}

const Ticket_t *QUEUE_GetCurrent(void)
{
    if (s_current == APP_QUEUE_NONE)
    {
        return NULL;
    }
    return &s_tickets[s_current];
}

uint16_t QUEUE_GetWaitCount(void)
{
    uint16_t i;
    uint16_t n = 0U;

    for (i = 0U; i < s_count; i++)
    {
        if (s_tickets[i].state == TICKET_WAIT)
        {
            n++;
        }
    }
    return n;
}

uint16_t QUEUE_GetPassedCount(void)
{
    uint16_t i;
    uint16_t n = 0U;

    for (i = 0U; i < s_count; i++)
    {
        if (s_tickets[i].state == TICKET_PASSED)
        {
            n++;
        }
    }
    return n;
}

uint16_t QUEUE_GetCount(void)
{
    return s_count;
}

uint32_t QUEUE_GetVersion(void)
{
    return s_version;
}

uint8_t QUEUE_TakeAnnounce(void)
{
    uint8_t flag = s_announce;

    s_announce = 0U;
    return flag;
}

uint8_t QUEUE_Append(uint8_t prefix, uint16_t seq)
{
    uint16_t i;
    uint16_t j;
    uint16_t cur_seq;

    /* 该序号已存在（无论什么状态），不重复追加 */
    for (i = 0U; i < s_count; i++)
    {
        if (s_tickets[i].seq == seq)
        {
            return 0U;
        }
    }

    /* 队列已满：清理已就诊完成(TICKET_DONE)的号，释放空间。
       过号(TICKET_PASSED)保留，因为可能被回呼。 */
    if (s_count >= APP_QUEUE_MAX)
    {
        cur_seq = (s_current != APP_QUEUE_NONE) ? s_tickets[s_current].seq : 0U;

        j = 0U;
        for (i = 0U; i < s_count; i++)
        {
            if (s_tickets[i].state != TICKET_DONE)
            {
                s_tickets[j] = s_tickets[i];
                j++;
            }
        }
        s_count = j;

        /* 清理后重新定位 current */
        if (cur_seq > 0U)
        {
            s_current = APP_QUEUE_NONE;
            for (i = 0U; i < s_count; i++)
            {
                if (s_tickets[i].seq == cur_seq)
                {
                    s_current = i;
                    break;
                }
            }
        }
    }

    /* 清理后仍满（无已完成号可清理），无法追加 */
    if (s_count >= APP_QUEUE_MAX)
    {
        return 0U;
    }

    s_tickets[s_count].prefix = prefix;
    s_tickets[s_count].seq    = seq;
    s_tickets[s_count].state  = TICKET_WAIT;
    s_count++;
    s_version++;
    return 1U;
}

uint16_t QUEUE_GetMaxSeq(void)
{
    uint16_t i;
    uint16_t max_seq = 0U;

    for (i = 0U; i < s_count; i++)
    {
        if (s_tickets[i].seq > max_seq)
        {
            max_seq = s_tickets[i].seq;
        }
    }
    return max_seq;
}

void QUEUE_FormatCode(const Ticket_t *ticket, char *buf)
{
    if ((ticket == NULL) || (buf == NULL))
    {
        return;
    }

    buf[0] = (char)ticket->prefix;
    buf[1] = (char)('0' + ((ticket->seq / 100U) % 10U));
    buf[2] = (char)('0' + ((ticket->seq / 10U) % 10U));
    buf[3] = (char)('0' + (ticket->seq % 10U));
    buf[4] = '\0';
}
