#ifndef LITE_LOADER_PORT_H
#define LITE_LOADER_PORT_H

#include <stdint.h>
#include <stdbool.h>

/*
 * loader_port_t - 硬件抽象层函数指针表
 * 使用者在调用 loader_port_init 之前填充所有字段
 */
typedef struct {
    /* 判断是否进入 bootloader 模式（如读取 GPIO） */
    bool (*is_boot_state)(void);

    /* Flash 操作 */
    bool (*flash_erase_page)(uint32_t page_addr);
    bool (*flash_write)(uint32_t addr, const uint8_t *data, uint32_t size);

    /* UART 操作 */
    bool (*uart_rx)(uint8_t *byte, uint32_t timeout_ms);
    bool (*uart_tx)(const uint8_t *data, uint32_t size);

    /* 延时 */
    void (*delay_ms)(uint32_t ms);

    /* 可选：系统复位（为 NULL 时使用默认 NVIC 复位） */
    void (*reset)(void);
} loader_port_t;

/* 校验端口（所有必填函数指针非空），返回 false 表示端口不完整 */
bool loader_port_init(loader_port_t *port);

/* bootloader 主入口，不会返回（除非跳转到应用程序） */
void boot_process(loader_port_t *port);

#endif /* LITE_LOADER_PORT_H */
