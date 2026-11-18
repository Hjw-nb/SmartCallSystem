/**
 * @file    st7735.h
 * @brief   ST7735S 1.8 寸 TFT（128x160）软件 SPI 驱动
 *
 * 接线（以立创 EDA 原理图 Sheet_1_2026-10-25 为准）：
 *   SCL -> PC8   SDA -> PC9   RST -> PC10
 *   DC  -> PB7   CS  -> PB8   BL  -> PB11
 *   VCC -> 3.3V  GND -> GND
 *
 * 注意：PC8/PC9 不是 STM32F103 的硬件 SPI 引脚，因此本驱动使用 GPIO 模拟 SPI。
 */

#ifndef __ST7735_H__
#define __ST7735_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 屏幕分辨率（竖屏） */
#define ST7735_WIDTH            128U
#define ST7735_HEIGHT           160U

/* 显示偏移：部分 1.8 寸模块需要设为 2/1，出现边缘错位时再修改 */
#define ST7735_X_OFFSET         2U
#define ST7735_Y_OFFSET         1U

/* RGB565 常用颜色 */
#define ST7735_BLACK            0x0000U
#define ST7735_WHITE            0xFFFFU
#define ST7735_RED              0xF800U
#define ST7735_GREEN            0x07E0U
#define ST7735_BLUE             0x001FU
#define ST7735_CYAN             0x07FFU
#define ST7735_MAGENTA          0xF81FU
#define ST7735_YELLOW           0xFFE0U
#define ST7735_ORANGE           0xFD20U
#define ST7735_GRAY             0x8410U

void ST7735_Init(void);
void ST7735_SetRotation(uint8_t rotation);
void ST7735_InvertDisplay(uint8_t invert);
void ST7735_Backlight(uint8_t on);

void ST7735_FillScreen(uint16_t color);
void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7735_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/* 8x16 ASCII 文字：index 0x20-0x7E，scale 为整数放大倍数 */
void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);

/* 16x16 单色点阵：每行 2 字节，共 32 字节，MSB 在左（中文点阵用） */
void ST7735_DrawBitmap16(uint16_t x, uint16_t y, const uint8_t *bitmap, uint16_t fg, uint16_t bg);
void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale);
uint16_t ST7735_TextWidth(const char *str, uint8_t scale);

/* 上电自检：纯色扫描 -> 8 色彩条 -> 边框/对角线 -> 中央色块 */
void ST7735_Test(void);

#ifdef __cplusplus
}
#endif

#endif /* __ST7735_H__ */
