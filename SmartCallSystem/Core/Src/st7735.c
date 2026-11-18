/**
 * @file    st7735.c
 * @brief   ST7735S 1.8 寸 TFT（128x160）GPIO 模拟 SPI 驱动
 *
 * SPI 时序：模式 0（空闲低电平，上升沿采样），高位先发（MSB First）。
 */

#include "st7735.h"
#include "font8x16.h"
#include <string.h>

/* ---- 快速 GPIO 操作（直接写 BSRR/BRR，比 HAL_GPIO_WritePin 快） ---- */
#define TFT_SCL_HIGH()      (TFT_SCL_GPIO_Port->BSRR = TFT_SCL_Pin)
#define TFT_SCL_LOW()       (TFT_SCL_GPIO_Port->BRR  = TFT_SCL_Pin)
#define TFT_SDA_HIGH()      (TFT_SDA_GPIO_Port->BSRR = TFT_SDA_Pin)
#define TFT_SDA_LOW()       (TFT_SDA_GPIO_Port->BRR  = TFT_SDA_Pin)
#define TFT_CS_HIGH()       (TFT_CS_GPIO_Port->BSRR  = TFT_CS_Pin)
#define TFT_CS_LOW()        (TFT_CS_GPIO_Port->BRR   = TFT_CS_Pin)
#define TFT_DC_HIGH()       (TFT_DC_GPIO_Port->BSRR  = TFT_DC_Pin)
#define TFT_DC_LOW()        (TFT_DC_GPIO_Port->BRR   = TFT_DC_Pin)
#define TFT_RST_HIGH()      (TFT_RST_GPIO_Port->BSRR = TFT_RST_Pin)
#define TFT_RST_LOW()       (TFT_RST_GPIO_Port->BRR  = TFT_RST_Pin)
#define TFT_BL_HIGH()       (TFT_BL_GPIO_Port->BSRR  = TFT_BL_Pin)
#define TFT_BL_LOW()        (TFT_BL_GPIO_Port->BRR   = TFT_BL_Pin)

static uint16_t s_width  = ST7735_WIDTH;
static uint16_t s_height = ST7735_HEIGHT;

/* ---- 底层字节/命令写入 ---- */
static void ST7735_WriteByte(uint8_t data)
{
    uint8_t i;

    for (i = 0; i < 8U; i++)
    {
        if (data & 0x80U)
        {
            TFT_SDA_HIGH();
        }
        else
        {
            TFT_SDA_LOW();
        }
        data <<= 1;
        TFT_SCL_HIGH();     /* ST7735 在上升沿采样 */
        TFT_SCL_LOW();
    }
}

static void ST7735_WriteCommand(uint8_t cmd)
{
    TFT_DC_LOW();
    TFT_CS_LOW();
    ST7735_WriteByte(cmd);
    TFT_CS_HIGH();
}

static void ST7735_WriteData(uint8_t data)
{
    TFT_DC_HIGH();
    TFT_CS_LOW();
    ST7735_WriteByte(data);
    TFT_CS_HIGH();
}

static void ST7735_WriteCommandData(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    ST7735_WriteCommand(cmd);
    while (len-- > 0U)
    {
        ST7735_WriteData(*data++);
    }
}

/* 连续写入 count 个同色像素（调用前已设置好显示窗口） */
static void ST7735_WriteColor(uint16_t color, uint32_t count)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFU);

    TFT_DC_HIGH();
    TFT_CS_LOW();
    while (count-- > 0U)
    {
        ST7735_WriteByte(hi);
        ST7735_WriteByte(lo);
    }
    TFT_CS_HIGH();
}

static void ST7735_SetAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x0 = (uint16_t)(x + ST7735_X_OFFSET);
    uint16_t y0 = (uint16_t)(y + ST7735_Y_OFFSET);
    uint16_t x1 = (uint16_t)(x0 + w - 1U);
    uint16_t y1 = (uint16_t)(y0 + h - 1U);

    ST7735_WriteCommand(0x2AU);                 /* CASET */
    ST7735_WriteData((uint8_t)(x0 >> 8));
    ST7735_WriteData((uint8_t)(x0 & 0xFFU));
    ST7735_WriteData((uint8_t)(x1 >> 8));
    ST7735_WriteData((uint8_t)(x1 & 0xFFU));

    ST7735_WriteCommand(0x2BU);                 /* RASET */
    ST7735_WriteData((uint8_t)(y0 >> 8));
    ST7735_WriteData((uint8_t)(y0 & 0xFFU));
    ST7735_WriteData((uint8_t)(y1 >> 8));
    ST7735_WriteData((uint8_t)(y1 & 0xFFU));

    ST7735_WriteCommand(0x2CU);                 /* RAMWR */
}

/* 把 TFT 相关引脚重新配置为高速推挽输出，保证软件 SPI 的边沿速度 */
static void ST7735_GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    GPIO_InitStruct.Pin = TFT_SCL_Pin | TFT_SDA_Pin | TFT_RST_Pin;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = TFT_DC_Pin | TFT_CS_Pin | TFT_BL_Pin;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void ST7735_Init(void)
{
    static const uint8_t frmctr1[] = {0x01U, 0x2CU, 0x2DU};
    static const uint8_t frmctr2[] = {0x01U, 0x2CU, 0x2DU};
    static const uint8_t frmctr3[] = {0x01U, 0x2CU, 0x2DU, 0x01U, 0x2CU, 0x2DU};
    static const uint8_t pwctr1[]  = {0xA2U, 0x02U, 0x84U};
    static const uint8_t pwctr2[]  = {0xC5U};
    static const uint8_t pwctr3[]  = {0x0AU, 0x00U};
    static const uint8_t pwctr4[]  = {0x8AU, 0x2AU};
    static const uint8_t pwctr5[]  = {0x8AU, 0xEEU};
    static const uint8_t vmctr1[]  = {0x0EU};
    static const uint8_t invctr[]  = {0x07U};
    static const uint8_t colmod[]  = {0x05U};   /* 16 bit/pixel (RGB565) */
    static const uint8_t gmctrp1[] = {0x02U, 0x1CU, 0x07U, 0x12U, 0x37U, 0x32U,
                                      0x29U, 0x2DU, 0x29U, 0x25U, 0x2BU, 0x39U,
                                      0x00U, 0x01U, 0x03U, 0x10U};
    static const uint8_t gmctrn1[] = {0x03U, 0x1DU, 0x07U, 0x06U, 0x2EU, 0x2CU,
                                      0x29U, 0x2DU, 0x2EU, 0x2EU, 0x37U, 0x3FU,
                                      0x00U, 0x00U, 0x02U, 0x10U};

    ST7735_GpioInit();

    TFT_CS_HIGH();
    TFT_DC_HIGH();
    TFT_SCL_LOW();
    TFT_SDA_LOW();
    TFT_BL_HIGH();                  /* 背光常亮 */

    /* 硬件复位 */
    TFT_RST_HIGH();
    HAL_Delay(10U);
    TFT_RST_LOW();
    HAL_Delay(20U);
    TFT_RST_HIGH();
    HAL_Delay(120U);

    ST7735_WriteCommand(0x01U);     /* SWRESET */
    HAL_Delay(120U);

    ST7735_WriteCommand(0x11U);     /* SLPOUT */
    HAL_Delay(120U);

    ST7735_WriteCommandData(0xB1U, frmctr1, sizeof(frmctr1));
    ST7735_WriteCommandData(0xB2U, frmctr2, sizeof(frmctr2));
    ST7735_WriteCommandData(0xB3U, frmctr3, sizeof(frmctr3));
    ST7735_WriteCommandData(0xB4U, invctr,  sizeof(invctr));
    ST7735_WriteCommandData(0xC0U, pwctr1,  sizeof(pwctr1));
    ST7735_WriteCommandData(0xC1U, pwctr2,  sizeof(pwctr2));
    ST7735_WriteCommandData(0xC2U, pwctr3,  sizeof(pwctr3));
    ST7735_WriteCommandData(0xC3U, pwctr4,  sizeof(pwctr4));
    ST7735_WriteCommandData(0xC4U, pwctr5,  sizeof(pwctr5));
    ST7735_WriteCommandData(0xC5U, vmctr1,  sizeof(vmctr1));
    ST7735_WriteCommandData(0x3AU, colmod,  sizeof(colmod));

    ST7735_SetRotation(0);

    ST7735_WriteCommandData(0xE0U, gmctrp1, sizeof(gmctrp1));
    ST7735_WriteCommandData(0xE1U, gmctrn1, sizeof(gmctrn1));

    ST7735_WriteCommand(0x13U);     /* NORON */
    ST7735_WriteCommand(0x29U);     /* DISPON */
    HAL_Delay(100U);
}

void ST7735_SetRotation(uint8_t rotation)
{
    uint8_t madctl;

    switch (rotation & 0x03U)
    {
        case 0:
        default:
            madctl = 0xC8U;         /* MX | MY | BGR */
            s_width  = ST7735_WIDTH;
            s_height = ST7735_HEIGHT;
            break;
        case 1:
            madctl = 0xA8U;         /* MY | MV | BGR */
            s_width  = ST7735_HEIGHT;
            s_height = ST7735_WIDTH;
            break;
        case 2:
            madctl = 0x08U;         /* BGR */
            s_width  = ST7735_WIDTH;
            s_height = ST7735_HEIGHT;
            break;
        case 3:
            madctl = 0x68U;         /* MX | MV | BGR */
            s_width  = ST7735_HEIGHT;
            s_height = ST7735_WIDTH;
            break;
    }

    ST7735_WriteCommand(0x36U);     /* MADCTL */
    ST7735_WriteData(madctl);
}

void ST7735_InvertDisplay(uint8_t invert)
{
    ST7735_WriteCommand(invert ? 0x21U : 0x20U);
}

void ST7735_Backlight(uint8_t on)
{
    if (on)
    {
        TFT_BL_HIGH();
    }
    else
    {
        TFT_BL_LOW();
    }
}

void ST7735_FillScreen(uint16_t color)
{
    ST7735_FillRect(0U, 0U, s_width, s_height, color);
}

void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((x >= s_width) || (y >= s_height) || (w == 0U) || (h == 0U))
    {
        return;
    }
    if ((x + w) > s_width)
    {
        w = (uint16_t)(s_width - x);
    }
    if ((y + h) > s_height)
    {
        h = (uint16_t)(s_height - y);
    }

    ST7735_SetAddrWindow(x, y, w, h);
    ST7735_WriteColor(color, (uint32_t)w * h);
}

void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if ((x >= s_width) || (y >= s_height))
    {
        return;
    }

    ST7735_SetAddrWindow(x, y, 1U, 1U);
    ST7735_WriteColor(color, 1U);
}

void ST7735_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((w == 0U) || (h == 0U))
    {
        return;
    }

    ST7735_FillRect(x, y, w, 1U, color);
    ST7735_FillRect(x, (uint16_t)(y + h - 1U), w, 1U, color);
    ST7735_FillRect(x, y, 1U, h, color);
    ST7735_FillRect((uint16_t)(x + w - 1U), y, 1U, h, color);
}

void ST7735_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);

    while (1)
    {
        if ((x0 >= 0) && (y0 >= 0))
        {
            ST7735_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
        }
        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }
        {
            int16_t e2 = (int16_t)(err * 2);
            if (e2 > -dy)
            {
                err = (int16_t)(err - dy);
                x0  = (int16_t)(x0 + sx);
            }
            if (e2 < dx)
            {
                err = (int16_t)(err + dx);
                y0  = (int16_t)(y0 + sy);
            }
        }
    }
}

void ST7735_Test(void)
{
    static const uint16_t full_colors[] = {
        ST7735_RED, ST7735_GREEN, ST7735_BLUE, ST7735_WHITE, ST7735_BLACK
    };
    static const uint16_t bars[] = {
        ST7735_RED, ST7735_ORANGE, ST7735_YELLOW, ST7735_GREEN,
        ST7735_CYAN, ST7735_BLUE, ST7735_MAGENTA, ST7735_WHITE
    };
    uint16_t bar_w = (uint16_t)(s_width / 8U);
    uint8_t i;

    /* 1. 纯色扫描，确认数据线/像素格式正常 */
    for (i = 0U; i < (uint8_t)(sizeof(full_colors) / sizeof(full_colors[0])); i++)
    {
        ST7735_FillScreen(full_colors[i]);
        HAL_Delay(400U);
    }

    /* 2. 8 色彩条，确认每个像素通道与窗口寻址正常 */
    ST7735_FillScreen(ST7735_BLACK);
    for (i = 0U; i < 8U; i++)
    {
        ST7735_FillRect((uint16_t)(i * bar_w), 0U, bar_w, s_height, bars[i]);
    }

    /* 3. 边框 + 对角线，确认坐标方向与偏移 */
    ST7735_DrawRect(0U, 0U, s_width, s_height, ST7735_WHITE);
    ST7735_DrawRect(1U, 1U, (uint16_t)(s_width - 2U), (uint16_t)(s_height - 2U), ST7735_WHITE);
    ST7735_DrawLine(0, 0, (int16_t)(s_width - 1U), (int16_t)(s_height - 1U), ST7735_RED);
    ST7735_DrawLine((int16_t)(s_width - 1U), 0, 0, (int16_t)(s_height - 1U), ST7735_GREEN);

    /* 4. 中央色块 */
    ST7735_FillRect((uint16_t)(s_width / 2U - 20U), (uint16_t)(s_height / 2U - 16U),
                    40U, 32U, ST7735_BLACK);
    ST7735_FillRect((uint16_t)(s_width / 2U - 16U), (uint16_t)(s_height / 2U - 12U),
                    32U, 24U, ST7735_CYAN);
}

void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *glyph;
    uint8_t code = (uint8_t)c;
    uint8_t row, col, sy, sx;
    uint8_t fh, fl, bh, bl;

    if (scale == 0U)
    {
        scale = 1U;
    }
    if ((x + (8U * scale)) > s_width)
    {
        return;
    }
    if ((y + (16U * scale)) > s_height)
    {
        return;
    }
    if ((code < 0x20U) || (code > 0x7EU))
    {
        code = (uint8_t)'?';
    }

    glyph = font8x16[code - 0x20U];
    fh = (uint8_t)(fg >> 8);
    fl = (uint8_t)(fg & 0xFFU);
    bh = (uint8_t)(bg >> 8);
    bl = (uint8_t)(bg & 0xFFU);

    ST7735_SetAddrWindow(x, y, (uint16_t)(8U * scale), (uint16_t)(16U * scale));
    TFT_DC_HIGH();
    TFT_CS_LOW();
    for (row = 0U; row < 16U; row++)
    {
        uint8_t bits = glyph[row];
        for (sy = 0U; sy < scale; sy++)
        {
            for (col = 0U; col < 8U; col++)
            {
                uint8_t is_fg = (uint8_t)((bits & (0x80U >> col)) != 0U);
                for (sx = 0U; sx < scale; sx++)
                {
                    if (is_fg)
                    {
                        ST7735_WriteByte(fh);
                        ST7735_WriteByte(fl);
                    }
                    else
                    {
                        ST7735_WriteByte(bh);
                        ST7735_WriteByte(bl);
                    }
                }
            }
        }
    }
    TFT_CS_HIGH();
}

void ST7735_DrawBitmap16(uint16_t x, uint16_t y, const uint8_t *bitmap, uint16_t fg, uint16_t bg)
{
    uint8_t row, col;
    uint8_t fh, fl, bh, bl;

    if (bitmap == NULL)
    {
        return;
    }
    if ((x + 16U) > s_width)
    {
        return;
    }
    if ((y + 16U) > s_height)
    {
        return;
    }

    fh = (uint8_t)(fg >> 8);
    fl = (uint8_t)(fg & 0xFFU);
    bh = (uint8_t)(bg >> 8);
    bl = (uint8_t)(bg & 0xFFU);

    ST7735_SetAddrWindow(x, y, 16U, 16U);
    TFT_DC_HIGH();
    TFT_CS_LOW();
    for (row = 0U; row < 16U; row++)
    {
        uint8_t bits_hi = bitmap[row * 2U];
        uint8_t bits_lo = bitmap[(row * 2U) + 1U];

        for (col = 0U; col < 16U; col++)
        {
            uint8_t is_fg;

            if (col < 8U)
            {
                is_fg = (uint8_t)((bits_hi & (0x80U >> col)) != 0U);
            }
            else
            {
                is_fg = (uint8_t)((bits_lo & (0x80U >> (col - 8U))) != 0U);
            }
            if (is_fg != 0U)
            {
                ST7735_WriteByte(fh);
                ST7735_WriteByte(fl);
            }
            else
            {
                ST7735_WriteByte(bh);
                ST7735_WriteByte(bl);
            }
        }
    }
    TFT_CS_HIGH();
}

void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t step;

    if ((str == NULL) || (scale == 0U))
    {
        return;
    }

    step = (uint16_t)(8U * scale);
    while (*str != '\0')
    {
        if ((x + step) > s_width)
        {
            break;
        }
        ST7735_DrawChar(x, y, *str, fg, bg, scale);
        x = (uint16_t)(x + step);
        str++;
    }
}

uint16_t ST7735_TextWidth(const char *str, uint8_t scale)
{
    if ((str == NULL) || (scale == 0U))
    {
        return 0U;
    }
    return (uint16_t)(strlen(str) * 8U * scale);
}
