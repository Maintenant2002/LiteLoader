#ifndef HAL_PORT_H
#define HAL_PORT_H

#include <stdint.h>
#include <stdbool.h>

/* 硬件抽象层 —— 所有函数指针必须由用户实现 */
typedef struct {
    /* 检测是否进入 Bootloader（如读取按键或 GPIO 电平） */
    bool (*check_boot_mode)(void);

    /* Flash 擦除与写入 */
    bool (*erase_flash_page)(uint32_t page_addr);
    bool (*write_flash)(uint32_t addr, const uint8_t *data, uint32_t size);

    /* UART 收发（带超时，单位 ms） */
    bool (*uart_receive_byte)(uint8_t *byte, uint32_t timeout_ms);
    bool (*uart_transmit)(const uint8_t *data, uint32_t size);

    /* 延时 */
    void (*delay_ms)(uint32_t ms);

    /* 系统复位（可为 NULL，此时使用默认软件复位） */
    void (*reset_system)(void);

    /* 调试输出（可选） */
    void (*debug_printf)(const char *fmt, ...);
} hal_port_t;

/* 校验 HAL 是否完整（所有必需函数非空） */
bool hal_port_validate(hal_port_t *hal);

#endif