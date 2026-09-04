#ifndef LITE_LOADER_CONF_H
#define LITE_LOADER_CONF_H

/*
 * LiteLoader 配置文件
 * 根据目标平台修改以下参数
 */

/* 应用程序起始地址（必须对齐到 Flash 页边界） */
#define CONF_APP_START_ADDR     0x08001000

/* 需要擦除的 Flash 页数（用于存放应用程序）
 * 计算公式：(APP_END - APP_START) / PAGE_SIZE */
#define CONF_APP_PAGE_COUNT     63

/* Flash 页大小（字节），因 STM32 系列而异 */
#define CONF_FLASH_PAGE_SIZE    1024

/* 固件最大尺寸（必须 <= APP_PAGE_COUNT * PAGE_SIZE） */
#define CONF_FW_MAX_SIZE        (CONF_APP_PAGE_COUNT * CONF_FLASH_PAGE_SIZE)

/* UART 接收超时时间（毫秒） */
#define CONF_UART_TIMEOUT_MS    5000

#endif /* LITE_LOADER_CONF_H */
