#ifndef BOOT_LOADER_H
#define BOOT_LOADER_H

#include "hal_port.h"
#include "proto.h"

/* 前向声明结构体标签 */
struct boot_context;

/* 状态处理函数原型 */
typedef void (*state_handler_t)(struct boot_context *ctx);

/* 状态描述 */
typedef struct {
    state_handler_t handler;
    const char *name;
} state_t;

/* 初始化配置（调用者填充） */
typedef struct {
    state_t *init_state;        /* 初始状态（通常 &state_check_boot_state） */
    hal_port_t *hal;            /* 硬件抽象层 */
    proto_t *proto;             /* 传输协议 */
    uint8_t *rx_buf;            /* 接收缓冲区（调用者分配） */
    uint32_t rx_buf_size;       /* 缓冲区大小 */
    uint32_t app_start_addr;    /* 应用起始地址 */
    uint32_t flash_page_size;   /* Flash 页大小 */
    uint32_t total_pages;       /* 需擦除的页数 */
} boot_config_t;

/* 主上下文（包含所有运行时数据） */
typedef struct boot_context {
    state_t *cur_state;
    hal_port_t *hal;
    proto_t *proto;
    void *proto_priv;              /* 协议私有数据 */
    uint8_t *rx_buf;               /* 接收缓冲区指针 */
    uint32_t rx_buf_size;          /* 缓冲区大小 */
    uint32_t app_start_addr;
    uint32_t flash_page_size;
    uint32_t total_pages;
    uint32_t current_write_addr;   /* 运行时更新 */
} boot_context_t;

/* 状态实例（states.c 中定义） */
extern state_t state_check_boot_state;
extern state_t state_protocol_init;
extern state_t state_receive;
extern state_t state_verify;
extern state_t state_error;

/* 公共接口 */
void boot_init(boot_context_t *ctx, const boot_config_t *cfg);
void change_state(boot_context_t *ctx, state_t *new_state);
void boot_handle(boot_context_t *ctx);

#endif /* BOOT_LOADER_H */
