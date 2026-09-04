/*
 * main_test.c - LiteLoader Windows 测试入口
 *
 * 用法：
 *   lite_loader_test <firmware.bin> [flash_out.bin]
 *
 * 参数：
 *   firmware.bin  - 由 gen_firmware.py 生成的帧数据文件
 *   flash_out.bin - flash 写入输出（默认 flash_out.bin）
 */

#include "loader.h"
#include "proto_custom.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* port_win32.c 提供的接口 */
extern int          port_win32_flash_open(const char *path);
extern void         port_win32_flash_close(void);
extern int          port_win32_uart_open(const char *path);
extern void         port_win32_uart_close(void);
extern loader_port_t port_win32_create(void);

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);  /* UTF-8 */
#endif

    if (argc < 2) {
        fprintf(stderr,
            "LiteLoader 测试工具\n"
            "用法: %s <firmware.bin> [flash_out.bin]\n"
            "\n"
            "  firmware.bin  - 由 gen_firmware.py 生成的帧数据文件\n"
            "  flash_out.bin - flash 输出文件（默认 flash_out.bin）\n",
            argv[0]);
        return 1;
    }

    const char *fw_path   = argv[1];
    const char *flash_out = (argc >= 3) ? argv[2] : "flash_out.bin";

    /* 打开固件帧文件（模拟 UART 输入） */
    if (port_win32_uart_open(fw_path) != 0) {
        return 1;
    }

    /* 打开 flash 输出文件 */
    if (port_win32_flash_open(flash_out) != 0) {
        port_win32_uart_close();
        return 1;
    }

    /* 构建 port 和 protocol */
    loader_port_t port = port_win32_create();
    const loader_protocol_t *proto = proto_custom_get();

    /* 接收缓冲区 */
    uint8_t buf[256];

    printf("=== LiteLoader 测试开始 ===\n");
    printf("固件输入: %s\n", fw_path);
    printf("Flash 输出: %s\n", flash_out);
    printf("协议: %s\n", proto->name ? proto->name : "(unnamed)");
    printf("===========================\n\n");

    /* 启动 bootloader（不会正常返回） */
    boot_process(&port, proto, buf, sizeof(buf));

    /* 清理（正常情况下不会执行到这里） */
    port_win32_flash_close();
    port_win32_uart_close();

    printf("\n=== 测试完成 ===\n");
    return 0;
}
