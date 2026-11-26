/**
 * @file OLED_Data.h
 * @brief OLED字模数据结构定义
 * @details 定义汉字字模的数据结构，包含编码索引和对应的点阵数据。
 */

#ifndef __OLED_DATA_H
#define __OLED_DATA_H

#include <stdint.h>

/*字符集定义（二选一）*/
#define OLED_CHARSET_UTF8			//UTF8字符集
// #define OLED_CHARSET_GB2312		//GB2312字符集

/*汉字字模单元结构*/
typedef struct 
{
#ifdef OLED_CHARSET_UTF8
    char Index[5];					//UTF8编码索引（5字节）
#endif
#ifdef OLED_CHARSET_GB2312
    char Index[3];					//GB2312编码索引（3字节）
#endif
    uint8_t Data[32];				//16x16汉字字模数据（32字节）
} ChineseCell_t;

/*ASCII字模数据声明*/
extern const uint8_t OLED_F8x16[][16];	//8x16 ASCII字模
extern const uint8_t OLED_F6x8[][6];	//6x8 ASCII字模

/*汉字字模数据声明*/
extern const ChineseCell_t OLED_CF16x16[];	//16x16汉字字模

/*图像数据声明*/
extern const uint8_t Diode[];	//二极管符号图像（16x16）

#endif


/*****************江协科技|版权所有****************/
/*****************jiangxiekeji.com*****************/
