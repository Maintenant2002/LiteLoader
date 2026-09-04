#include "loader.h"
#include "conf.h"

/* ============================================================
 *  ARM Cortex-M 跳转辅助
 * ============================================================ */

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

    __set_MSP(app_stack);

    void (*entry)(void) = (void (*)(void))app_entry;
    entry();
}

/* ============================================================
 *  公共 API
 * ============================================================ */

bool loader_port_init(loader_port_t *port)
{
    if (port == NULL) return false;
    if (port->is_boot_state   == NULL) return false;
    if (port->flash_erase_page == NULL) return false;
    if (port->flash_write     == NULL) return false;
    if (port->uart_rx         == NULL) return false;
    if (port->uart_tx         == NULL) return false;
    if (port->delay_ms        == NULL) return false;
    /* port->reset 是可选的 */
    return true;
}

void boot_process(loader_port_t *port,
                  const loader_protocol_t *proto,
                  uint8_t *buf,
                  uint32_t buf_size)
{
    loader_ctx_t ctx;

    ctx.state       = STATE_IDLE;
    ctx.fw_received = 0;
    ctx.last_error  = 0;
    ctx.port        = *port;
    ctx.proto       = *proto;
    ctx.rx_buf      = buf;
    ctx.rx_buf_size = buf_size;

    /* 检查是否进入 bootloader 模式 */
    if (!ctx.port.is_boot_state()) {
        jump_to_app(CONF_APP_START_ADDR);
        return;
    }

    /* 主循环 */
    while (ctx.state != STATE_JUMP && ctx.state != STATE_ERROR) {
        switch (ctx.state) {

        /* ---- 协议握手 ---- */
        case STATE_IDLE:
            if (ctx.proto.init(&ctx.port)) {
                ctx.state = STATE_ERASE;
            } else {
                ctx.last_error = 1;
                ctx.state = STATE_ERROR;
            }
            break;

        /* ---- 擦除 Flash ---- */
        case STATE_ERASE: {
            uint32_t addr = CONF_APP_START_ADDR;
            bool ok = true;
            for (uint32_t i = 0; i < CONF_APP_PAGE_COUNT; i++) {
                if (!ctx.port.flash_erase_page(addr)) {
                    ok = false;
                    break;
                }
                addr += CONF_FLASH_PAGE_SIZE;
            }
            ctx.state = ok ? STATE_RECV : STATE_ERROR;
            if (!ok) ctx.last_error = 2;
            break;
        }

        /* ---- 循环接收数据块并写入 Flash ---- */
        case STATE_RECV: {
            uint32_t actual_len = 0;
            if (!ctx.proto.receive_chunk(&ctx.port,
                                         ctx.rx_buf,
                                         ctx.rx_buf_size,
                                         &actual_len)) {
                ctx.last_error = 3;
                ctx.state = STATE_ERROR;
                break;
            }

            /* actual_len == 0：传输结束 */
            if (actual_len == 0) {
                ctx.state = ctx.proto.verify ? STATE_VERIFY : STATE_JUMP;
                break;
            }

            /* 写入 Flash */
            uint32_t write_addr = CONF_APP_START_ADDR + ctx.fw_received;
            if (!ctx.port.flash_write(write_addr, ctx.rx_buf, actual_len)) {
                ctx.last_error = 4;
                ctx.state = STATE_ERROR;
                break;
            }
            ctx.fw_received += actual_len;
            break;
        }

        /* ---- 整体校验（可选） ---- */
        case STATE_VERIFY:
            if (ctx.proto.verify) {
                if (ctx.proto.verify(&ctx.port)) {
                    ctx.state = STATE_JUMP;
                } else {
                    ctx.last_error = 5;
                    ctx.state = STATE_ERROR;
                }
            } else {
                ctx.state = STATE_JUMP;
            }
            break;

        default:
            ctx.last_error = 0xFF;
            ctx.state = STATE_ERROR;
            break;
        }
    }

    /* 传输结束通知 */
    if (ctx.proto.finish) {
        ctx.proto.finish(&ctx.port);
    }

    if (ctx.state == STATE_JUMP) {
        jump_to_app(CONF_APP_START_ADDR);
    }

    /* STATE_ERROR：死循环等待外部复位 */
    while (1) {}
}
