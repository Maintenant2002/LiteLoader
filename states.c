// states.c
#include "boot_loader.h"
#include <string.h>

/* ============================================================
 *  ARM Cortex-M 跳转辅助（仅 ARM 平台编译）
 * ============================================================ */

#ifdef USING_ARM_CHIP

#ifndef __set_MSP
static inline void __set_MSP(uint32_t topOfMainStack)
{
    __asm volatile ("MSR msp, %0\n" : : "r" (topOfMainStack) : );
}
#endif

static void jump_to_app(uint32_t app_addr)
{
    uint32_t app_stack = *(volatile uint32_t *)(app_addr);
    uint32_t app_entry = *(volatile uint32_t *)(app_addr + 4);

    /* 关闭全局中断 */
    __asm volatile ("cpsid i" ::: "memory");

    /* 关闭 SysTick */
    *((volatile uint32_t *)0xE000E010) = 0;

    /* 向量表偏移指向应用程序 */
    *((volatile uint32_t *)0xE000ED08) = app_addr;

    __set_MSP(app_stack);

    void (*entry)(void) = (void (*)(void))app_entry;
    entry();
}

#endif /* USING_ARM_CHIP */

extern proto_t proto_xmodem;

/* ============================================================
 *  公共接口实现
 * ============================================================ */

void boot_init(boot_context_t *ctx, state_t *init_state,
               hal_port_t *hal, proto_t *proto,
               uint32_t app_start, uint32_t page_size, uint32_t page_count)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->cur_state       = init_state;
    ctx->hal             = hal;
    ctx->proto           = proto;
    ctx->app_start_addr  = app_start;
    ctx->flash_page_size = page_size;
    ctx->total_pages     = page_count;
}

void change_state(boot_context_t *ctx, state_t *new_state)
{
    ctx->cur_state = new_state;
}

void boot_handle(boot_context_t *ctx)
{
    /* 持续派发状态处理函数，直到进入 ERROR 状态 */
    while (ctx->cur_state != &state_error) {
        ctx->cur_state->handler(ctx);
    }
    /* ERROR 状态处理一次 */
    ctx->cur_state->handler(ctx);
}

/* ============================================================
 *  HAL 校验
 * ============================================================ */

bool hal_port_validate(hal_port_t *hal)
{
    if (hal == NULL) return false;
    if (hal->check_boot_mode  == NULL) return false;
    if (hal->erase_flash_page == NULL) return false;
    if (hal->write_flash      == NULL) return false;
    if (hal->uart_receive_byte == NULL) return false;
    if (hal->uart_transmit    == NULL) return false;
    if (hal->delay_ms         == NULL) return false;
    /* reset_system 和 debug_printf 可选 */
    return true;
}

/* ============================================================
 *  状态实现
 * ============================================================ */

/* 状态：检测启动模式 */
static void handler_check_boot(boot_context_t *ctx)
{
    hal_port_t *hal = ctx->hal;

    if (hal->check_boot_mode()) {
        change_state(ctx, &state_protocol_init);
    } else {
#ifdef USING_ARM_CHIP
        jump_to_app(ctx->app_start_addr);
#else
        if (hal->debug_printf)
            hal->debug_printf("[boot] Not in boot mode, would jump to app.\n");
        change_state(ctx, &state_error);
#endif
    }
}

/* 状态：协议初始化 */
static void handler_protocol_init(boot_context_t *ctx)
{
    if (ctx->proto->init(ctx)) {
        /* 擦除所有 APP 页 */
        uint32_t page = ctx->app_start_addr;
        for (uint32_t i = 0; i < ctx->total_pages; i++) {
            if (!ctx->hal->erase_flash_page(page)) {
                change_state(ctx, &state_error);
                return;
            }
            page += ctx->flash_page_size;
        }
        ctx->current_write_addr = ctx->app_start_addr;
        change_state(ctx, &state_receive);
    } else {
        change_state(ctx, &state_error);
    }
}

/* 状态：接收数据块并写入 Flash */
static void handler_receive(boot_context_t *ctx)
{
    uint8_t buffer[128];
    uint32_t len = 0;

    bool result = ctx->proto->receive_chunk(ctx, buffer, sizeof(buffer), &len);
    if (!result) {
        change_state(ctx, &state_error);
        return;
    }
    if (len == 0) {
        /* 传输结束 */
        change_state(ctx, &state_verify);
        return;
    }
    /* 写入 Flash */
    if (!ctx->hal->write_flash(ctx->current_write_addr, buffer, len)) {
        change_state(ctx, &state_error);
        return;
    }
    ctx->current_write_addr += len;
    /* 状态不变，继续接收下一块 */
}

/* 状态：校验并跳转 */
static void handler_verify(boot_context_t *ctx)
{
    if (ctx->proto->verify && !ctx->proto->verify(ctx)) {
        change_state(ctx, &state_error);
        return;
    }
    if (ctx->proto->finish)
        ctx->proto->finish(ctx);

#ifdef USING_ARM_CHIP
    jump_to_app(ctx->app_start_addr);
#else
    if (ctx->hal->debug_printf)
        ctx->hal->debug_printf("[boot] Verification passed, would jump to app.\n");
    change_state(ctx, &state_error);  /* 测试模式下以此结束 */
#endif
}

/* 状态：错误处理 */
static void handler_error(boot_context_t *ctx)
{
    if (ctx->hal->debug_printf)
        ctx->hal->debug_printf("[boot] Error occurred!\n");
    ctx->hal->delay_ms(100);
#ifdef USING_ARM_CHIP
    if (ctx->hal->reset_system) ctx->hal->reset_system();
    *((volatile uint32_t *)0xE000ED0C) = 0x05FA0004;  /* NVIC 软件复位 */
    while (1) {}
#else
    /* 测试环境下不复位，直接返回 */
#endif
}

/* ============================================================
 *  状态实例定义
 * ============================================================ */

state_t state_check_boot_state = { .handler = handler_check_boot,   .name = "CHECK_BOOT" };
state_t state_protocol_init    = { .handler = handler_protocol_init, .name = "PROTO_INIT" };
state_t state_receive          = { .handler = handler_receive,       .name = "RECEIVE" };
state_t state_verify           = { .handler = handler_verify,        .name = "VERIFY" };
state_t state_error            = { .handler = handler_error,         .name = "ERROR" };
