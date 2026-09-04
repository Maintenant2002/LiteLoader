/*
 * port_win32.c - LiteLoader Windows 测试用硬件抽象层实现
 *
 * flash 操作 → 命令行输出 + 文件保存
 * uart 操作  → 命令行输出 + 文件读取
 * delay_ms   → Sleep()
 */

#include "port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

/* ============================================================
 *  Flash 模拟：写入文件
 * ============================================================ */

static FILE *s_flash_file = NULL;

int port_win32_flash_open(const char *path)
{
    s_flash_file = fopen(path, "wb");
    if (!s_flash_file) {
        fprintf(stderr, "[port] 无法打开输出文件: %s\n", path);
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

static bool win_flash_erase_page(uint32_t page_addr)
{
    printf("[flash] 擦除页 0x%08X\n", page_addr);
    return true;
}

static bool win_flash_write(uint32_t addr, const uint8_t *data, uint32_t size)
{
    if (!s_flash_file) {
        fprintf(stderr, "[flash] 错误：输出文件未打开\n");
        return false;
    }

    /* 按地址偏移写入文件（减去 APP 基地址） */
    long target = (long)addr - (long)0x08001000;

    if (target >= 0) {
        fseek(s_flash_file, target, SEEK_SET);
    }

    size_t written = fwrite(data, 1, size, s_flash_file);
    if (written != size) {
        fprintf(stderr, "[flash] 写入失败：请求 %u 字节，实际写入 %zu\n",
                size, written);
        return false;
    }

    printf("[flash] 写入 0x%08X  %u 字节\n", addr, size);
    return true;
}

/* ============================================================
 *  UART 模拟：从文件读取 / 输出到控制台
 * ============================================================ */

static FILE *s_uart_file = NULL;

int port_win32_uart_open(const char *path)
{
    s_uart_file = fopen(path, "rb");
    if (!s_uart_file) {
        fprintf(stderr, "[port] 无法打开固件文件: %s\n", path);
        return -1;
    }
    fseek(s_uart_file, 0, SEEK_END);
    long file_size = ftell(s_uart_file);
    fseek(s_uart_file, 0, SEEK_SET);
    printf("[port] 固件帧文件：%s（%ld 字节）\n", path, file_size);
    return 0;
}

void port_win32_uart_close(void)
{
    if (s_uart_file) {
        fclose(s_uart_file);
        s_uart_file = NULL;
    }
}

static bool win_uart_rx(uint8_t *byte, uint32_t timeout_ms)
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

static bool win_uart_tx(const uint8_t *data, uint32_t size)
{
    printf("[uart tx] ");
    for (uint32_t i = 0; i < size; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
    return true;
}

/* ============================================================
 *  延时 / 调试
 * ============================================================ */

static void win_delay_ms(uint32_t ms)
{
    Sleep(ms);
}

static void win_test(const char *str)
{
    printf("[test] %s", str);
}

static bool win_is_boot_state(void)
{
    return true;  /* 测试模式下始终进入 bootloader */
}

/* ============================================================
 *  组装 port
 * ============================================================ */

loader_port_t port_win32_create(void)
{
    loader_port_t port;
    memset(&port, 0, sizeof(port));

    port.is_boot_state    = win_is_boot_state;
    port.flash_erase_page = win_flash_erase_page;
    port.flash_write      = win_flash_write;
    port.uart_rx          = win_uart_rx;
    port.uart_tx          = win_uart_tx;
    port.delay_ms         = win_delay_ms;
    port.reset            = NULL;
    port.test             = win_test;

    return port;
}
