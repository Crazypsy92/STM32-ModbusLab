/**
 * @file OLED.c
 * @brief OLED显示屏驱动实现
 * @details 包含OLED初始化、光标设置、图形绘制、字符/数字显示等功能的实现，
 * 支持I2C通信（含DMA模式），适配128x64分辨率OLED屏幕。
 * @author 西伯利亚小菜狗
 * @date 2025-12-13
 * @version 1.0
 */

#include "oled.h"
#include "oled_data.h"
#include "oled_port.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* 创建OLED设备 */
OLED_Device g_oled;

// 内部函数声明
static void OLED_WriteCommand(OLED_Device *dev, uint8_t Command);
static void OLED_WriteData(OLED_Device *dev, uint8_t *data, uint8_t len);
static void OLED_SetCursor(OLED_Device *dev, uint8_t Page, uint8_t Column);
static uint32_t OLED_Pow(uint32_t X, uint32_t Y);

/**
 * @brief 初始化OLED设备
 * @param[in] dev OLED设备指针，需提前分配内存
 * @note 底层引脚与总线由 oled_port.c 的 OLED_PortInit() 完成
 */
void OLED_Init(OLED_Device *dev) {
    // 清空显存和错误状态
    memset(dev->DisplayBuf, 0, sizeof(dev->DisplayBuf));
    dev->Error = 0U;

    // 初始化底层总线（GPIO / I2C 外设）
    OLED_PortInit();

    // OLED初始化序列
    OLED_WriteCommand(dev, 0xAE);  // 关闭显示
    
    OLED_WriteCommand(dev, 0xD5);  // 设置显示时钟分频
    OLED_WriteCommand(dev, 0x80);
    
    OLED_WriteCommand(dev, 0xA8);  // 设置多路复用率
    OLED_WriteCommand(dev, 0x3F);
    
    OLED_WriteCommand(dev, 0xD3);  // 设置显示偏移
    OLED_WriteCommand(dev, 0x00);
    
    OLED_WriteCommand(dev, 0x40);  // 设置显示起始行
    
    OLED_WriteCommand(dev, 0xA1);  // 左右反置
    OLED_WriteCommand(dev, 0xC8);  // 上下反置
    
    OLED_WriteCommand(dev, 0xDA);  // 设置COM引脚配置
    OLED_WriteCommand(dev, 0x12);
    
    OLED_WriteCommand(dev, 0x81);  // 设置对比度
    OLED_WriteCommand(dev, 0xCF);
    
    OLED_WriteCommand(dev, 0xD9);  // 设置预充电周期
    OLED_WriteCommand(dev, 0xF1);
    
    OLED_WriteCommand(dev, 0xDB);  // 设置VCOMH
    OLED_WriteCommand(dev, 0x30);
    
    OLED_WriteCommand(dev, 0xA4);  // 整体显示开启
    OLED_WriteCommand(dev, 0xA6);  // 正常显示
    
    OLED_WriteCommand(dev, 0x8D);  // 开启电荷泵
    OLED_WriteCommand(dev, 0x14);
    
    OLED_WriteCommand(dev, 0xAF);  // 开启显示
}

/**
 * @brief 设置光标位置
 * @param dev OLED设备指针
 * @param Page 页地址(0-7)
 * @param Column 列地址(0-127)
 */
static void OLED_SetCursor(OLED_Device *dev, uint8_t Page, uint8_t Column) {
    OLED_WriteCommand(dev, 0xB0 + Page);                  // 设置页地址
    OLED_WriteCommand(dev, 0x00 + (Column & 0x0F));       // 设置列地址低4位
    OLED_WriteCommand(dev, 0x10 + ((Column >> 4) & 0x0F));// 设置列地址高4位
}

/**
 * @brief 向OLED写入命令
 * @param dev OLED设备指针（保留参数，便于上层统一调用）
 * @param Command 命令值
 */
static void OLED_WriteCommand(OLED_Device *dev, uint8_t Command) {
    if (OLED_PortWriteCmd(Command) != OLED_PORT_OK) {
        dev->Error = 1U;
    }
}

/**
 * @brief 向OLED写入数据
 * @param dev OLED设备指针（保留参数，便于上层统一调用）
 * @param data 数据起始地址
 * @param len 数据数量
 */
static void OLED_WriteData(OLED_Device *dev, uint8_t *data, uint8_t len) {
    if (OLED_PortWriteData(data, len) != OLED_PORT_OK) {
        dev->Error = 1U;
    }
}


/**
 * @brief 清空OLED显存
 * @param dev OLED设备指针
 */
void OLED_Clear(OLED_Device *dev) {
    memset(dev->DisplayBuf, 0, sizeof(dev->DisplayBuf));
}

/**
 * @brief 获取总线错误标志
 * @param dev OLED设备指针
 * @return 0=正常，非0=发生过 IIC 发送失败
 */
uint8_t OLED_GetError(OLED_Device *dev) {
    return dev->Error;
}

/**
 * @brief 清除总线错误状态
 * @param dev OLED设备指针
 */
void OLED_ClearError(OLED_Device *dev) {
    dev->Error = 0U;
}

/**
 * @brief 更新OLED显示(全部区域)
 * @param dev OLED设备指针
 */
void OLED_Update(OLED_Device *dev) {
    for (uint8_t j = 0; j < 8; j++) {
        OLED_SetCursor(dev, j, 0);
        OLED_WriteData(dev, dev->DisplayBuf[j], 128);
    }
}

/**
 * @brief 更新OLED显示(指定区域)
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标
 * @param Width 宽度
 * @param Height 高度
 */
void OLED_UpdateArea(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height) {
    int16_t j;
    int16_t Page, Page1;
    int16_t startX, endX;

    if (Width == 0U || Height == 0U) {
        return;
    }

    Page = Y / 8;
    Page1 = (Y + Height - 1) / 8 + 1;
    if (Y < 0) {
        Page -= 1;
        Page1 -= 1;
    }

    /* 列方向裁剪到 0~127，避免数据越界写到下一行显存 */
    startX = (X < 0) ? 0 : X;
    endX = X + Width;
    if (endX > 128) {
        endX = 128;
    }
    if (startX >= endX) {
        return;
    }

    for (j = Page; j < Page1; j++) {
        if (j >= 0 && j <= 7) {
            OLED_SetCursor(dev, j, startX);
            OLED_WriteData(dev, &dev->DisplayBuf[j][startX], (uint8_t)(endX - startX));
        }
    }
}

/**
 * @brief 在显存中画点
 * @param dev OLED设备指针
 * @param X X坐标
 * @param Y Y坐标
 */
void OLED_DrawPoint(OLED_Device *dev, int16_t X, int16_t Y) {
    if (X >= 0 && X < 128 && Y >= 0 && Y < 64) {
        dev->DisplayBuf[Y / 8][X] |= 1 << (Y % 8);
    }
}

/**
 * @brief 显示图像到OLED指定位置
 * @param[in] dev OLED设备指针
 * @param[in] X 图像左上角X坐标（范围：0~127）
 * @param[in] Y 图像左上角Y坐标（范围：0~63）
 * @param[in] Width 图像宽度（像素）
 * @param[in] Height 图像高度（像素）
 * @param[in] Image 图像数据指针（点阵数据，每个bit代表一个像素）
 * @note 图像数据需与屏幕分辨率匹配，超出屏幕范围的部分将被截断
 * @warning Image指针需指向有效内存，否则可能导致内存访问错误
 */
void OLED_ShowImage(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image) {
    uint8_t i, j;
    int16_t PageY = Y / 8;
    uint8_t PagesHeight = Height / 8; // 图片占多少页

    if (Image == NULL || Width == 0U || Height == 0U) {
        return;
    }
    if (Y < 0 || Y >= 64 || X >= 128) {
        return;
    }

    for (j = 0; j < PagesHeight; j++) {
        if (PageY + j > 7) {
            break;  /* 超出屏幕底部 */
        }
        for (i = 0; i < Width; i++) {
            int16_t x = X + i;
            if (x >= 0 && x < 128) {
                // Image排列顺序：先第一页所有列，再第二页所有列...
                // 目标显存：PageY + j
                dev->DisplayBuf[PageY + j][x] = Image[j * Width + i];
            }
        }
    }
}

/**
 * @brief 显示字符 (优化版：直接写显存，修复乱码问题)
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标 (必须是8的倍数，如0, 8, 16...)
 * @param Char 要显示的字符
 * @param FontSize 字体大小
 */
void OLED_ShowChar(OLED_Device *dev, int16_t X, int16_t Y, char Char, uint8_t FontSize) {
    uint8_t i;
    const uint8_t *pFont;
    int16_t PageY;

    /* 只支持可打印 ASCII，避免使用非 ASCII 字节索引字模导致越界 */
    if (Char < ' ' || Char > '~' || Y < 0 || Y >= 64) {
        return;
    }
    PageY = Y / 8;

    if (FontSize == OLED_8X16) {
        // 获取该字符的字模首地址
        pFont = &OLED_F8x16[Char - ' '][0];

        // 绘制上半部分 (8x8) -> 对应 PageY
        for (i = 0; i < 8; i++) {
            int16_t x = X + i;
            if (x >= 0 && x < 128) {
                dev->DisplayBuf[PageY][x] = pFont[i];
            }
        }

        // 绘制下半部分 (8x8) -> 对应 PageY + 1（超出屏幕则截断）
        if (PageY + 1 < 8) {
            for (i = 0; i < 8; i++) {
                int16_t x = X + i;
                if (x >= 0 && x < 128) {
                    dev->DisplayBuf[PageY + 1][x] = pFont[i + 8];
                }
            }
        }
    }
    else if (FontSize == OLED_6X8) {
        // 获取6x8字模
        pFont = &OLED_F6x8[Char - ' '][0];

        // 6x8只有一页
        for (i = 0; i < 6; i++) {
            int16_t x = X + i;
            if (x >= 0 && x < 128) {
                dev->DisplayBuf[PageY][x] = pFont[i];
            }
        }
    }
}

/**
 * @brief 显示字符串
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标
 * @param String 要显示的字符串
 * @param FontSize 字体大小
 */
void OLED_ShowString(OLED_Device *dev, int16_t X, int16_t Y, char *String, uint8_t FontSize) {
    uint8_t i = 0;
    uint8_t charWidth = (FontSize == OLED_8X16) ? 8 : 6;
    
    while (String[i] != '\0') {
        OLED_ShowChar(dev, X + i * charWidth, Y, String[i], FontSize);
        i++;
    }
}

/**
 * @brief 次方函数
 * @param X 底数
 * @param Y 指数
 * @return X^Y的结果
 */
static uint32_t OLED_Pow(uint32_t X, uint32_t Y) {
    uint32_t Result = 1;
    while (Y--) {
        Result *= X;
    }
    return Result;
}

/**
 * @brief 显示无符号数字
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标
 * @param Number 要显示的数字
 * @param Length 显示长度
 * @param FontSize 字体大小
 */
void OLED_ShowNum(OLED_Device *dev, int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize) {
    uint8_t i;
    uint8_t charWidth = (FontSize == OLED_8X16) ? 8 : 6;
    
    for (i = 0; i < Length; i++) {
        OLED_ShowChar(dev, X + i * charWidth, Y, 
                    (Number / OLED_Pow(10, Length - i - 1) % 10) + '0', 
                    FontSize);
    }
}

/**
 * @brief 显示有符号数字
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标
 * @param Number 要显示的数字
 * @param Length 显示长度
 * @param FontSize 字体大小
 */
void OLED_ShowSignedNum(OLED_Device *dev, int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize) {
    uint8_t i;
    uint32_t Number1;
    uint8_t charWidth = (FontSize == OLED_8X16) ? 8 : 6;
    
    if (Number >= 0) {
        OLED_ShowChar(dev, X, Y, '+', FontSize);
        Number1 = Number;
    } else {
        OLED_ShowChar(dev, X, Y, '-', FontSize);
        /* 避免 INT32_MIN 直接取负溢出 */
        Number1 = (uint32_t)(-(Number + 1)) + 1U;
    }
    
    for (i = 0; i < Length; i++) {
        OLED_ShowChar(dev, X + (i + 1) * charWidth, Y, 
                    (Number1 / OLED_Pow(10, Length - i - 1) % 10) + '0', 
                    FontSize);
    }
}

/**
 * @brief 显示十六进制数字
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标
 * @param Number 要显示的数字
 * @param Length 显示长度
 * @param FontSize 字体大小
 */
void OLED_ShowHexNum(OLED_Device *dev, int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize) {
    uint8_t i, SingleNumber;
    uint8_t charWidth = (FontSize == OLED_8X16) ? 8 : 6;
    
    for (i = 0; i < Length; i++) {
        SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
        if (SingleNumber < 10) {
            OLED_ShowChar(dev, X + i * charWidth, Y, SingleNumber + '0', FontSize);
        } else {
            OLED_ShowChar(dev, X + i * charWidth, Y, SingleNumber - 10 + 'A', FontSize);
        }
    }
}

/**
 * @brief 显示汉字/中英混排字符串（纯中文字符为16像素宽，ASCII为8像素宽，自动累加光标）
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标（需为8的倍数）
 * @param Chinese 待显示的字符串（UTF-8 编码）
 * @note 字库中不存在的汉字会跳过对应宽度，不会绘制乱码
 */
void OLED_ShowChinese(OLED_Device *dev, int16_t X, int16_t Y, char *Chinese) {
    int16_t cursor = X;
    int16_t PageY;
    uint8_t i = 0;
    char SingleChinese[OLED_CHN_CHAR_WIDTH + 1];
    uint8_t n, pIndex;

    if (Chinese == NULL || Y < 0 || Y >= 64) {
        return;
    }
    PageY = Y / 8;

    while (Chinese[i] != '\0') {
        /* ASCII 半角字符：按 8x16 字体绘制，占 8 像素宽 */
        if ((Chinese[i] & 0x80) == 0) {
            OLED_ShowChar(dev, cursor, Y, Chinese[i], OLED_8X16);
            cursor += 8;
            i++;
            continue;
        }

        /* UTF-8 三字节汉字：取 3 字节编码后在字库中查表 */
        memset(SingleChinese, 0, sizeof(SingleChinese));
        for (n = 0; n < OLED_CHN_CHAR_WIDTH && Chinese[i] != '\0'; n++) {
            SingleChinese[n] = Chinese[i++];
        }

        for (pIndex = 0; OLED_CF16x16[pIndex].Index[0] != '\0'; pIndex++) {
            if (strcmp(OLED_CF16x16[pIndex].Index, SingleChinese) == 0) {
                break;
            }
        }

        /* 字库无此字：跳过，避免把结束标志的图像数据画上去 */
        if (OLED_CF16x16[pIndex].Index[0] == '\0') {
            cursor += 16;
            continue;
        }

        /* 16x16 字模：前 16 字节为上半部分（PageY），后 16 字节为下半部分（PageY+1） */
        const uint8_t *pData = OLED_CF16x16[pIndex].Data;
        for (uint8_t k = 0; k < 16; k++) {
            if (cursor + k < 128 && cursor + k >= 0) {
                dev->DisplayBuf[PageY][cursor + k] = pData[k];
                if (PageY + 1 < 8) {
                    dev->DisplayBuf[PageY + 1][cursor + k] = pData[k + 16];
                }
            }
        }
        cursor += 16;
    }
}

/**
 * @brief 格式化打印
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标
 * @param FontSize 字体大小
 * @param format 格式化字符串
 * @param ... 可变参数
 */
void OLED_Printf(OLED_Device *dev, int16_t X, int16_t Y, uint8_t FontSize, char *format, ...) {
    char String[256];
    va_list arg;
    
    va_start(arg, format);
    vsnprintf(String, sizeof(String), format, arg);
    va_end(arg);
    
    OLED_ShowString(dev, X, Y, String, FontSize);
}

/**
 * @brief 显存全部取反
 * @param dev OLED设备指针
 */
void OLED_Reverse(OLED_Device *dev) {
    for (uint8_t j = 0; j < 8; j++) {
        for (uint8_t i = 0; i < 128; i++) {
            dev->DisplayBuf[j][i] ^= 0xFF;
        }
    }
}

/**
 * @brief 显存指定区域取反
 * @param dev OLED设备指针
 * @param X 左上角X坐标
 * @param Y 左上角Y坐标
 * @param Width 宽度
 * @param Height 高度
 */
void OLED_ReverseArea(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height) {
    for (int16_t j = Y; j < Y + Height; j++) {
        for (int16_t i = X; i < X + Width; i++) {
            if (i >= 0 && i <= 127 && j >= 0 && j <= 63) {
                dev->DisplayBuf[j / 8][i] ^= 0x01 << (j % 8);
            }
        }
    }
}

/**
 * @brief 画线
 * @param dev OLED设备指针
 * @param X0 起点X坐标
 * @param Y0 起点Y坐标
 * @param X1 终点X坐标
 * @param Y1 终点Y坐标
 */
void OLED_DrawLine(OLED_Device *dev, int16_t X0, int16_t Y0, int16_t X1, int16_t Y1) {
    int16_t x, y, dx, dy, d, incrE, incrNE, temp;
    int16_t x0 = X0, y0 = Y0, x1 = X1, y1 = Y1;
    uint8_t yflag = 0, xyflag = 0;
    
    if (y0 == y1) {  // 水平线
        if (x0 > x1) { temp = x0; x0 = x1; x1 = temp; }
        for (x = x0; x <= x1; x++) {
            OLED_DrawPoint(dev, x, y0);
        }
    } else if (x0 == x1) {  // 垂直线
        if (y0 > y1) { temp = y0; y0 = y1; y1 = temp; }
        for (y = y0; y <= y1; y++) {
            OLED_DrawPoint(dev, x0, y);
        }
    } else {  // 斜线(Bresenham算法)
        if (x0 > x1) {
            temp = x0; x0 = x1; x1 = temp;
            temp = y0; y0 = y1; y1 = temp;
        }
        
        if (y0 > y1) {
            y0 = -y0;
            y1 = -y1;
            yflag = 1;
        }
        
        if (y1 - y0 > x1 - x0) {
            temp = x0; x0 = y0; y0 = temp;
            temp = x1; x1 = y1; y1 = temp;
            xyflag = 1;
        }
        
        dx = x1 - x0;
        dy = y1 - y0;
        incrE = 2 * dy;
        incrNE = 2 * (dy - dx);
        d = 2 * dy - dx;
        x = x0;
        y = y0;
        
        if (yflag && xyflag) OLED_DrawPoint(dev, y, -x);
        else if (yflag) OLED_DrawPoint(dev, x, -y);
        else if (xyflag) OLED_DrawPoint(dev, y, x);
        else OLED_DrawPoint(dev, x, y);
        
        while (x < x1) {
            x++;
            if (d < 0) {
                d += incrE;
            } else {
                y++;
                d += incrNE;
            }
            
            if (yflag && xyflag) OLED_DrawPoint(dev, y, -x);
            else if (yflag) OLED_DrawPoint(dev, x, -y);
            else if (xyflag) OLED_DrawPoint(dev, y, x);
            else OLED_DrawPoint(dev, x, y);
        }
    }
}

/**
 * @brief OLED矩形
 * @param dev OLED设备指针
 * @param X 指定矩形左上角的横坐标
 * @param Y 指定矩形左上角的纵坐标
 * @param Width 指定矩形的宽度
 * @param Height 指定矩形的高度
 * @param IsFilled 指定矩形是否填充
 */
void OLED_DrawRectangle(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled) {
    int16_t i, j;
    if (!IsFilled) {  // 指定矩形不填充
        /*遍历上下X坐标，画矩形上下两条线*/
        for (i = X; i < X + Width; i++) {
            OLED_DrawPoint(dev, i, Y);
            OLED_DrawPoint(dev, i, Y + Height - 1);
        }
        /*遍历左右Y坐标，画矩形左右两条线*/
        for (i = Y; i < Y + Height; i++) {
            OLED_DrawPoint(dev, X, i);
            OLED_DrawPoint(dev, X + Width - 1, i);
        }
    } else {  // 指定矩形填充
        /*遍历X坐标*/
        for (i = X; i < X + Width; i++) {
            /*遍历Y坐标*/
            for (j = Y; j < Y + Height; j++) {
                /*在指定区域画点，填充满矩形*/
                OLED_DrawPoint(dev, i, j);
            }
        }
    }
}

/**
 * @brief 判断点是否在多边形内部（PNPOLY算法）
 * @param[in] dev OLED设备指针（未使用，预留扩展）
 * @param[in] nvert 多边形顶点数量（至少3个）
 * @param[in] vertx 顶点X坐标数组（长度为nvert）
 * @param[in] verty 顶点Y坐标数组（长度为nvert）
 * @param[in] testx 测试点X坐标
 * @param[in] testy 测试点Y坐标
 * @retval 1 测试点在多边形内部
 * @retval 0 测试点在多边形外部
 * @note 算法通过射线法判断点与多边形的位置关系，适用于任意简单多边形
 */
uint8_t OLED_pnpoly(OLED_Device *dev, uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy) {
    int16_t i, j, c = 0;
    for (i = 0, j = nvert - 1; i < nvert; j = i++) {
        if (((verty[i] > testy) != (verty[j] > testy)) &&
            (testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i])) {
            c = !c;
        }
    }
    return c;
}

/**
  * 函    数：OLED三角形
  * 参    数：X0 指定第一个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y0 指定第一个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：X1 指定第二个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y1 指定第二个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：X2 指定第三个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y2 指定第三个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：IsFilled 指定三角形是否填充
  *           范围：OLED_UNFILLED		不填充
  *                 OLED_FILLED			填充
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawTriangle(OLED_Device *dev, int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled){
    	int16_t minx = X0, miny = Y0, maxx = X0, maxy = Y0;
	int16_t i, j;
	int16_t vx[] = {X0, X1, X2};
	int16_t vy[] = {Y0, Y1, Y2};
	
	if (!IsFilled)			//指定三角形不填充
	{
		/*调用画线函数，将三个点用直线连接*/
		OLED_DrawLine(dev,X0, Y0, X1, Y1);
		OLED_DrawLine(dev,X0, Y0, X2, Y2);
		OLED_DrawLine(dev,X1, Y1, X2, Y2);
	}
	else					//指定三角形填充
	{
		/*找到三个点最小的X、Y坐标*/
		if (X1 < minx) {minx = X1;}
		if (X2 < minx) {minx = X2;}
		if (Y1 < miny) {miny = Y1;}
		if (Y2 < miny) {miny = Y2;}
		
		/*找到三个点最大的X、Y坐标*/
		if (X1 > maxx) {maxx = X1;}
		if (X2 > maxx) {maxx = X2;}
		if (Y1 > maxy) {maxy = Y1;}
		if (Y2 > maxy) {maxy = Y2;}
		
		/*最小最大坐标之间的矩形为可能需要填充的区域*/
		/*遍历此区域中所有的点*/
		/*遍历X坐标*/		
		for (i = minx; i <= maxx; i ++)
		{
			/*遍历Y坐标*/	
			for (j = miny; j <= maxy; j ++)
			{
				/*调用OLED_pnpoly，判断指定点是否在指定三角形之中*/
				/*如果在，则画点，如果不在，则不做处理*/
				if (OLED_pnpoly(dev,3, vx, vy, i, j)) {OLED_DrawPoint(dev,i, j);}
			}
		}
	}

}

/**
 * @brief 画椭圆
 * @param X 圆心X坐标
 * @param Y 圆心Y坐标
 * @param A 横向半轴
 * @param B 纵向半轴
 * @param IsFilled 是否填充
 */
void OLED_DrawEllipse(OLED_Device *dev, int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled){

        int16_t x, y, j;
    int16_t a = A, b = B;
    float d1, d2;
    
    x = 0;
    y = b;
    d1 = b * b + a * a * (-b + 0.5);
    
    if (IsFilled) {
        for (j = -y; j < y; j++) {
            OLED_DrawPoint(dev,X, Y + j);
            OLED_DrawPoint(dev,X, Y + j);
        }
    }
    
    OLED_DrawPoint(dev,X + x, Y + y);
    OLED_DrawPoint(dev,X - x, Y - y);
    OLED_DrawPoint(dev,X - x, Y + y);
    OLED_DrawPoint(dev,X + x, Y - y);
    
    while (b * b * (x + 1) < a * a * (y - 0.5)) {
        if (d1 <= 0) {
            d1 += b * b * (2 * x + 3);
        } else {
            d1 += b * b * (2 * x + 3) + a * a * (-2 * y + 2);
            y--;
        }
        x++;
        
        if (IsFilled) {
            for (j = -y; j < y; j++) {
                OLED_DrawPoint(dev,X + x, Y + j);
                OLED_DrawPoint(dev,X - x, Y + j);
            }
        }
        
        OLED_DrawPoint(dev,X + x, Y + y);
        OLED_DrawPoint(dev,X - x, Y - y);
        OLED_DrawPoint(dev,X - x, Y + y);
        OLED_DrawPoint(dev,X + x, Y - y);
    }
    
    d2 = b * b * (x + 0.5) * (x + 0.5) + a * a * (y - 1) * (y - 1) - a * a * b * b;
    
    while (y > 0) {
        if (d2 <= 0) {
            d2 += b * b * (2 * x + 2) + a * a * (-2 * y + 3);
            x++;
        } else {
            d2 += a * a * (-2 * y + 3);
        }
        y--;
        
        if (IsFilled) {
            for (j = -y; j < y; j++) {
                OLED_DrawPoint(dev,X + x, Y + j);
                OLED_DrawPoint(dev,X - x, Y + j);
            }
        }
        
        OLED_DrawPoint(dev,X + x, Y + y);
        OLED_DrawPoint(dev,X - x, Y - y);
        OLED_DrawPoint(dev,X - x, Y + y);
        OLED_DrawPoint(dev,X + x, Y - y);
    }

}

/**
 * @brief 在OLED上绘制圆形
 * @param[in] dev OLED设备指针
 * @param[in] X 圆心X坐标（范围：0~127）
 * @param[in] Y 圆心Y坐标（范围：0~63）
 * @param[in] Radius 圆的半径（像素）
 * @param[in] IsFilled 是否填充圆形（1=填充，0=仅画边框）
 * @note 采用中点圆算法绘制，支持空心和实心两种模式
 * @warning 半径过大可能导致圆形超出屏幕范围，超出部分不显示
 */
void OLED_DrawCircle(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled) {
    int16_t x, y, d, j;
    
    d = 1 - Radius;
    x = 0;
    y = Radius;
    
    /*画每个八分之一圆弧的起始点*/
    OLED_DrawPoint(dev, X + x, Y + y);
    OLED_DrawPoint(dev, X - x, Y - y);
    OLED_DrawPoint(dev, X + y, Y + x);
    OLED_DrawPoint(dev, X - y, Y - x);
    
    if (IsFilled) {  // 指定圆填充
        /*遍历起始点Y坐标*/
        for (j = -y; j < y; j++) {
            /*在指定区域画点，填充部分圆*/
            OLED_DrawPoint(dev, X, Y + j);
        }
    }
    
    while (x < y) {  // 遍历X轴的每个点
        x++;
        if (d < 0) {  // 下一个点在当前点东方
            d += 2 * x + 1;
        } else {  // 下一个点在当前点东南方
            y--;
            d += 2 * (x - y) + 1;
        }
        
        /*画每个八分之一圆弧的点*/
        OLED_DrawPoint(dev, X + x, Y + y);
        OLED_DrawPoint(dev, X + y, Y + x);
        OLED_DrawPoint(dev, X - x, Y - y);
        OLED_DrawPoint(dev, X - y, Y - x);
        OLED_DrawPoint(dev, X + x, Y - y);
        OLED_DrawPoint(dev, X + y, Y - x);
        OLED_DrawPoint(dev, X - x, Y + y);
        OLED_DrawPoint(dev, X - y, Y + x);
        
        if (IsFilled) {  // 指定圆填充
            /*遍历中间部分*/
            for (j = -y; j < y; j++) {
                /*在指定区域画点，填充部分圆*/
                OLED_DrawPoint(dev, X + x, Y + j);
                OLED_DrawPoint(dev, X - x, Y + j);
            }
            
            /*遍历两侧部分*/
            for (j = -x; j < x; j++) {
                /*在指定区域画点，填充部分圆*/
                OLED_DrawPoint(dev, X - y, Y + j);
                OLED_DrawPoint(dev, X + y, Y + j);
            }
        }
    }
}
