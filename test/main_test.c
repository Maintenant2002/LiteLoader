/*
 * main_test.c - LiteLoader Windows 测试入口
 *
 * 用法：
 *   lite_loader_test <firmware.bin> [flash_out.bin]
 */

#include "boot_loader.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* port_win32.c 提供的接口 */
extern int         port_win32_flash_open(const char *path);
extern void        port_win32_flash_close(void);
extern int         port_win32_uart_open(const char *path);
extern void        port_win32_uart_close(void);
extern hal_port_t  port_win32_create(void);

/* proto_xmodem.c 提供的协议实例 */
extern proto_t proto_xmodem;

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    if (argc < 2) {
        fprintf(stderr,
            "LiteLoader Test\n"
            "Usage: %s <firmware.bin> [flash_out.bin]\n"
            "\n"
            "  firmware.bin  - XMODEM-CRC frame file (from gen_firmware.py)\n"
            "  flash_out.bin - flash output file (default: flash_out.bin)\n",
            argv[0]);
        return 1;
    }

    const char *fw_path   = argv[1];
    const char *flash_out = (argc >= 3) ? argv[2] : "flash_out.bin";

    /* 打开固件文件（模拟 UART 输入） */
    if (port_win32_uart_open(fw_path) != 0) return 1;

    /* 打开 Flash 输出文件 */
    if (port_win32_flash_open(flash_out) != 0) {
        port_win32_uart_close();
        return 1;
    }

    /* 构建配置 */
    hal_port_t hal = port_win32_create();
    uint8_t rx_buf[128];

    boot_config_t cfg = {
        .init_state      = &state_check_boot_state,
        .hal             = &hal,
        .proto           = &proto_xmodem,
        .rx_buf          = rx_buf,
        .rx_buf_size     = sizeof(rx_buf),
        .app_start_addr  = 0x08001000,
        .flash_page_size = 1024,
        .total_pages     = 63,
    };

    /* 初始化并运行 bootloader */
    boot_context_t ctx;
    boot_init(&ctx, &cfg);

    printf("=== LiteLoader Test ===\n");
    printf("Firmware: %s\n", fw_path);
    printf("Flash output: %s\n", flash_out);
    printf("Protocol: %s\n", cfg.proto->name ? cfg.proto->name : "(unnamed)");
    printf("=======================\n\n");

    boot_handle(&ctx);

    /* 清理 */
    port_win32_flash_close();
    port_win32_uart_close();

    printf("\n=== Test Complete ===\n");
    return 0;
}
