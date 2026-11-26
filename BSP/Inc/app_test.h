/**
 * @file app_test.h
 * @brief 应用层测试模块接口（裸机版）
 * @details OLED 上电自测 + AT24xx EEPROM 读写自测/按键交互测试。
 *          测试逻辑集中在本模块，main.c 只负责底层初始化和周期调度。
 * @author 西伯利亚小菜狗
 * @version 3.0 (at24xx)
 */

#ifndef __APP_TEST_H_
#define __APP_TEST_H_

/**
 * @brief 测试初始化：OLED 自测 → 主界面 → AT24xx 读写自测
 * @note  需在 Timebase_Init()、KEY_Init()、OLED_Init()/OLED_Clear()、
 *        AT24XX_PortInit() 之后调用；OLED 自测与 EEPROM 读写自测均为阻塞流程
 */
void AppTest_Init(void);

/**
 * @brief 测试周期任务：按键扫描、事件处理、运行指示刷新
 * @note  非阻塞，由主循环按 1ms 心跳调用，内部自行按 5ms/1s 节拍调度
 */
void AppTest_Task(void);

#endif /* __APP_TEST_H_ */
