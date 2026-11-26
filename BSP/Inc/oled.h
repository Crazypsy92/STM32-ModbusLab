/**
 * @file OLED.c
 * @brief OLED显示屏驱动实现
 * @details 包含OLED初始化、光标设置、图形绘制、字符/数字显示等功能的实现，
 * 支持I2C通信（含DMA模式），适配128x64分辨率OLED屏幕。
 * @author 西伯利亚小菜狗
 * @date 2025-12-13
 * @version 1.0
 */

#ifndef OLED_H
#define OLED_H

#include <stdint.h>

// 字体大小定义
#define OLED_6X8  0
#define OLED_8X16 1

// 填充模式定义
#define OLED_UNFILLED 0
#define OLED_FILLED   1

// 汉字字符宽度(UTF-8编码)
#define OLED_CHN_CHAR_WIDTH 3

typedef struct {  // 补充typedef，避免声明时需要struct关键字
    uint8_t DisplayBuf[8][128];  // 显存缓冲区
    uint8_t Error;               // 总线错误标志：0=正常，非0=发生过 IIC 发送失败
} OLED_Device;  


// 函数声明（底层总线由 oled_port.c 负责，驱动不感知 MCU 细节）
void OLED_Init(OLED_Device *dev);
void OLED_Clear(OLED_Device *dev);
void OLED_Update(OLED_Device *dev);
void OLED_UpdateArea(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height);
void OLED_DrawPoint(OLED_Device *dev, int16_t X, int16_t Y);
void OLED_DrawLine(OLED_Device *dev, int16_t X0, int16_t Y0, int16_t X1, int16_t Y1);
void OLED_DrawRectangle(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled);
uint8_t OLED_pnpoly(OLED_Device *dev, uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy);
void OLED_DrawTriangle(OLED_Device *dev, int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled);
void OLED_DrawEllipse(OLED_Device *dev, int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled);
void OLED_DrawCircle(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled);
void OLED_Reverse(OLED_Device *dev);
void OLED_ReverseArea(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height);
void OLED_ShowChar(OLED_Device *dev, int16_t X, int16_t Y, char Char, uint8_t FontSize);
void OLED_ShowString(OLED_Device *dev, int16_t X, int16_t Y, char *String, uint8_t FontSize);
void OLED_ShowNum(OLED_Device *dev, int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowSignedNum(OLED_Device *dev, int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowHexNum(OLED_Device *dev, int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowChinese(OLED_Device *dev, int16_t X, int16_t Y, char *Chinese);
void OLED_ShowImage(OLED_Device *dev, int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image);
void OLED_Printf(OLED_Device *dev, int16_t X, int16_t Y, uint8_t FontSize, char *format, ...);

/** @brief 获取最近一次总线错误状态（0=正常，非0=发生过 IIC 发送失败） */
uint8_t OLED_GetError(OLED_Device *dev);

/** @brief 清除总线错误状态 */
void OLED_ClearError(OLED_Device *dev);

extern OLED_Device g_oled;

#endif
