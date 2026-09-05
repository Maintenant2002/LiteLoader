#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

/* 应用起始地址（必须页对齐） */
#define APP_START_ADDR      0x08001000

/* Flash 页大小（STM32 F1/F4 通常 1KB 或 2KB，需按实际修改） */
#define FLASH_PAGE_SIZE     1024

/* 需要擦除的页数（覆盖整个 APP 区域） */
#define APP_PAGE_COUNT      63

/* 最大固件大小 */
#define FW_MAX_SIZE         (APP_PAGE_COUNT * FLASH_PAGE_SIZE)

/* UART 超时（接收一个字节的最大等待时间） */
#define UART_TIMEOUT_MS     1000

/* XMODEM 超时（等待第一个包） */
#define XMODEM_INIT_TIMEOUT 5000

#endif