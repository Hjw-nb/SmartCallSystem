/**
 * @file    font16_cn.h
 * @brief   16x16 汉字点阵索引（界面固定用字）
 *
 * 字模由 font16_cn.c 提供，顺序必须与枚举一致；新增汉字需同时改
 * font16_cn.c 与这里，禁止插入到中间。
 */

#ifndef __FONT16_CN_H__
#define __FONT16_CN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    CN_ZHEN = 0,    /* 诊 */
    CN_SHI,         /* 室 */
    CN_YI,          /* 已 */
    CN_LIAN_NET,    /* 联 */
    CN_WANG,        /* 网 */
    CN_WEI,         /* 未 */
    CN_LIAN_CONN,   /* 连 */
    CN_JIE,         /* 接 */
    CN_ZHONG,       /* 中 */
    CN_ZHENG,       /* 正 */
    CN_ZAI,         /* 在 */
    CN_JIAO,        /* 叫 */
    CN_HAO,         /* 号 */
    CN_QING,        /* 请 */
    CN_DAO,         /* 到 */
    CN_DENG,        /* 等 */
    CN_HOU,         /* 候 */
    CN_GUO,         /* 过 */
    CN_ZAN,         /* 暂 */
    CN_TING,        /* 停 */
    CN_QUE,         /* 确 */
    CN_REN,         /* 认 */
    CN_HUI,         /* 回 */
    CN_HU,          /* 呼 */
    CN_QU,          /* 取 */
    CN_XIAO,        /* 消 */
    CN_AN,          /* 按 */
    CN_COUNT
} CNGlyph_t;

/* 在 (x,y) 画一个 16x16 汉字，id 为 CNGlyph_t */
void CN_DrawGlyph(uint16_t x, uint16_t y, uint8_t id, uint16_t fg, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif /* __FONT16_CN_H__ */
