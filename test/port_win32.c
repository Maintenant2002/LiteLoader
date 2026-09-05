/*
 * port_win32.c - LiteLoader Windows 测试用硬件抽象层实现
 *
 * hal_port_t 接口实现：
 *   flash 操作 → 命令行输出 + 文件保存
 *   uart 操作  → 命令行输出 + 文件读取
 *   delay_ms   → Sleep()
 */

#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif

#include "hal_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((uint32_t)(ms) * 1000)
#endif

/* ---- Flash 模拟：写入文件 ---- */

static FILE *s_flash_file = NULL;

int port_win32_flash_open(const char *path)
{
    s_flash_file = fopen(path, "wb");
    if (!s_flash_file) {
        fprintf(stderr, "[port] Cannot open output file: %s\n", path);
        return -1;
    }
    return 0;
}

void port_win32_flash_close(void)
{
    if (s_flash_file) {
        fclose(s_flash_file);
        s_flash_file = NULL;
    }
}

static bool win_erase_flash_page(uint32_t page_addr)
{
    printf("[flash] Erase page 0x%08X\n", page_addr);
    return true;
}

static bool win_write_flash(uint32_t addr, const uint8_t *data, uint32_t size)
{
    if (!s_flash_file) {
        fprintf(stderr, "[flash] Error: output file not open\n");
        return false;
    }

    /* 按地址偏移写入文件 */
    long target = (long)addr - 0x08001000;
    if (target >= 0) {
        fseek(s_flash_file, target, SEEK_SET);
    }

    size_t written = fwrite(data, 1, size, s_flash_file);
    if (written != size) {
        fprintf(stderr, "[flash] Write failed: requested %u, wrote %zu\n", size, written);
        return false;
    }

    printf("[flash] Write 0x%08X  %u bytes\n", addr, size);
    return true;
}

/* ---- UART 模拟：从文件读取 / 输出到控制台 ---- */

static FILE *s_uart_file = NULL;

int port_win32_uart_open(const char *path)
{
    s_uart_file = fopen(path, "rb");
    if (!s_uart_file) {
        fprintf(stderr, "[port] Cannot open firmware file: %s\n", path);
        return -1;
    }
    fseek(s_uart_file, 0, SEEK_END);
    long file_size = ftell(s_uart_file);
    fseek(s_uart_file, 0, SEEK_SET);
    printf("[port] Firmware file: %s (%ld bytes)\n", path, file_size);
    return 0;
}

void port_win32_uart_close(void)
{
    if (s_uart_file) {
        fclose(s_uart_file);
        s_uart_file = NULL;
    }
}

static bool win_uart_receive_byte(uint8_t *byte, uint32_t timeout_ms)
{
    if (!s_uart_file) return false;

    int ch = fgetc(s_uart_file);
    if (ch == EOF) {
        Sleep(timeout_ms);
        return false;
    }

    *byte = (uint8_t)ch;
    return true;
}

static bool win_uart_transmit(const uint8_t *data, uint32_t size)
{
    printf("[uart tx]");
    for (uint32_t i = 0; i < size; i++) {
        printf(" %02X", data[i]);
    }
    printf("\n");
    return true;
}

/* ---- 延时 ---- */

static void win_delay_ms(uint32_t ms)
{
    Sleep(ms);
}

/* ---- 启动模式检测 ---- */

static bool win_check_boot_mode(void)
{
    return true;  /* 测试模式下始终进入 bootloader */
}

/* ---- 调试输出 ---- */

static void win_debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* ---- 组装 hal_port_t ---- */

hal_port_t port_win32_create(void)
{
    hal_port_t hal;
    memset(&hal, 0, sizeof(hal));

    hal.check_boot_mode  = win_check_boot_mode;
    hal.erase_flash_page = win_erase_flash_page;
    hal.write_flash      = win_write_flash;
    hal.uart_receive_byte = win_uart_receive_byte;
    hal.uart_transmit    = win_uart_transmit;
    hal.delay_ms         = win_delay_ms;
    hal.reset_system     = NULL;
    hal.debug_printf     = win_debug_printf;

    return hal;
}
