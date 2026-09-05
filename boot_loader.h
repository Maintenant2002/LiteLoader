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

/* 主上下文（包含所有运行时数据） */
typedef struct boot_context {
    state_t *cur_state;
    hal_port_t *hal;
    proto_t *proto;
    void *proto_priv;              /* 协议私有数据（XMODEM 状态机用） */
    uint32_t app_start_addr;
    uint32_t flash_page_size;
    uint32_t total_pages;
    uint32_t current_write_addr;
} boot_context_t;

/* 状态实例（states.c 中定义） */
extern state_t state_check_boot_state;
extern state_t state_protocol_init;
extern state_t state_receive;
extern state_t state_verify;
extern state_t state_error;

/* 公共接口 */
void boot_init(boot_context_t *ctx, state_t *init_state,
               hal_port_t *hal, proto_t *proto,
               uint32_t app_start, uint32_t page_size, uint32_t page_count);
void change_state(boot_context_t *ctx, state_t *new_state);
void boot_handle(boot_context_t *ctx);

#endif /* BOOT_LOADER_H */
