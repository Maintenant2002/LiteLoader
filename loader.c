#include "loader.h"
#include "conf.h"

/* ============================================================
 *  内部辅助函数
 * ============================================================ */

/* CRC32 nibble 查表（16 项，比完整 256 项表节省 960 字节 Flash） */
static const uint32_t crc32_nibble_table[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
    0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_nibble_table[(crc ^ data[i]) & 0x0F] ^ (crc >> 4);
        crc = crc32_nibble_table[(crc ^ (data[i] >> 4)) & 0x0F] ^ (crc >> 4);
    }
    return crc;
}

/* XOR 校验和 */
static uint8_t xor_checksum(const uint8_t *buf, uint32_t len)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum ^= buf[i];
    }
    return sum;
}

/* 发送响应帧：MAGIC | STATUS | CRC */
static void send_response(loader_ctx_t *ctx, uint8_t status)
{
    uint8_t resp[3];
    resp[0] = PROTO_MAGIC;
    resp[1] = status;
    resp[2] = resp[0] ^ resp[1];  /* XOR 校验 */
    ctx->port.uart_tx(resp, 3);
}

/* 接收一个完整帧到 ctx->rx_buf
 * 帧格式：MAGIC | SEQ | LEN | DATA[LEN] | CRC
 * 返回 true 表示收到有效帧 */
static bool receive_frame(loader_ctx_t *ctx)
{
    uint8_t *buf = ctx->rx_buf;

    /* 1. 等待 MAGIC 字节 */
    while (1) {
        if (!ctx->port.uart_rx(&buf[0], CONF_UART_TIMEOUT_MS)) {
            return false;  /* 超时 */
        }
        if (buf[0] == PROTO_MAGIC) {
            break;
        }
    }

    /* 2. 读取 SEQ + LEN（2 字节） */
    if (!ctx->port.uart_rx(&buf[1], CONF_UART_TIMEOUT_MS)) return false;
    if (!ctx->port.uart_rx(&buf[2], CONF_UART_TIMEOUT_MS)) return false;

    uint8_t seq = buf[1];
    uint8_t len = buf[2];
    (void)seq;  /* seq 在帧头已记录，供调用者使用 */

    /* 3. 读取 DATA（len 字节） */
    for (uint8_t i = 0; i < len; i++) {
        if (!ctx->port.uart_rx(&buf[3 + i], CONF_UART_TIMEOUT_MS)) {
            return false;
        }
    }

    /* 4. 读取 CRC（1 字节） */
    if (!ctx->port.uart_rx(&buf[3 + len], CONF_UART_TIMEOUT_MS)) {
        return false;
    }

    /* 5. 校验：XOR(seq, len, data[0..len-1]) == crc */
    uint8_t calc_crc = xor_checksum(&buf[1], 2 + len);  /* seq + len + data */
    if (calc_crc != buf[3 + len]) {
        return false;
    }

    return true;
}

/* ARM 内联汇编辅助（如编译器不提供 __set_MSP） */
#ifndef __set_MSP
static inline void __set_MSP(uint32_t topOfMainStack)
{
    __asm volatile ("MSR msp, %0\n" : : "r" (topOfMainStack) : );
}
#endif

/* ============================================================
 *  跳转到应用程序（ARM Cortex-M 标准方式）
 * ============================================================ */

static void jump_to_app(uint32_t app_addr)
{
    /* 从应用程序的向量表读取初始栈指针和复位向量 */
    uint32_t app_stack  = *(volatile uint32_t *)(app_addr);
    uint32_t app_entry  = *(volatile uint32_t *)(app_addr + 4);

    /* 设置 MSP */
    __set_MSP(app_stack);

    /* 跳转 */
    void (*entry)(void) = (void (*)(void))app_entry;
    entry();
}

/* ============================================================
 *  状态处理函数
 * ============================================================ */

/* IDLE：等待 CMD_START */
static void state_idle(loader_ctx_t *ctx)
{
    if (!receive_frame(ctx)) {
        /* 超时或帧错误，留在 IDLE */
        send_response(ctx, PROTO_NACK);
        return;
    }

    uint8_t opcode = ctx->rx_buf[3];
    if (opcode == CMD_START && ctx->rx_buf[2] == 5) {
        /* CMD_START：DATA = opcode(1) + size(4) = 5 字节 */
        ctx->fw_size = (uint32_t)ctx->rx_buf[4]
                     | ((uint32_t)ctx->rx_buf[5] << 8)
                     | ((uint32_t)ctx->rx_buf[6] << 16)
                     | ((uint32_t)ctx->rx_buf[7] << 24);

        if (ctx->fw_size == 0 || ctx->fw_size > CONF_FW_MAX_SIZE) {
            send_response(ctx, PROTO_NACK);
            return;
        }

        ctx->fw_received = 0;
        ctx->fw_crc32    = 0xFFFFFFFF;
        ctx->expected_seq = 1;  /* CMD_START 已用 seq=0，数据帧从 1 开始 */

        send_response(ctx, PROTO_ACK);
        ctx->state = STATE_ERASE;
    } else {
        send_response(ctx, PROTO_NACK);
    }
}

/* ERASE：擦除应用程序区所有 Flash 页 */
static void state_erase(loader_ctx_t *ctx)
{
    uint32_t addr = CONF_APP_START_ADDR;
    for (uint32_t i = 0; i < CONF_APP_PAGE_COUNT; i++) {
        if (!ctx->port.flash_erase_page(addr)) {
            ctx->last_error = 1;
            ctx->state = STATE_ERROR;
            return;
        }
        addr += CONF_FLASH_PAGE_SIZE;
    }
    ctx->state = STATE_RECV;
}

/* RECV：接收数据帧并写入 Flash */
static void state_recv(loader_ctx_t *ctx)
{
    if (!receive_frame(ctx)) {
        send_response(ctx, PROTO_NACK);
        return;
    }

    uint8_t seq    = ctx->rx_buf[1];
    uint8_t len    = ctx->rx_buf[2];
    uint8_t opcode = ctx->rx_buf[3];

    if (opcode != CMD_DATA) {
        send_response(ctx, PROTO_NACK);
        return;
    }

    /* 序列号校验 */
    if (seq != ctx->expected_seq) {
        if (seq == (uint8_t)(ctx->expected_seq - 1)) {
            /* 重复帧，回复上一次的 ACK 但不重写 */
            send_response(ctx, PROTO_ACK);
        } else {
            send_response(ctx, PROTO_NACK);
        }
        return;
    }

    /* 固件数据在 rx_buf[4 .. 4+len-1]，len 包含 opcode，实际数据 = len - 1 */
    uint32_t data_len = len - 1;
    uint32_t write_addr = CONF_APP_START_ADDR + ctx->fw_received;

    /* 更新 CRC32 */
    ctx->fw_crc32 = crc32_update(ctx->fw_crc32, &ctx->rx_buf[4], data_len);

    /* 写入 Flash */
    if (!ctx->port.flash_write(write_addr, &ctx->rx_buf[4], data_len)) {
        ctx->last_error = 2;
        send_response(ctx, PROTO_NACK);
        ctx->state = STATE_ERROR;
        return;
    }

    ctx->fw_received += data_len;
    ctx->expected_seq++;

    send_response(ctx, PROTO_ACK);

    /* 检查是否已接收全部数据 */
    if (ctx->fw_received >= ctx->fw_size) {
        ctx->fw_crc32 ^= 0xFFFFFFFF;  /* CRC32 最终异或 */
        ctx->state = STATE_VERIFY;
    }
}

/* VERIFY：等待 CMD_VERIFY 并校验 CRC32 */
static void state_verify(loader_ctx_t *ctx)
{
    if (!receive_frame(ctx)) {
        send_response(ctx, PROTO_NACK);
        return;
    }

    uint8_t len    = ctx->rx_buf[2];
    uint8_t opcode = ctx->rx_buf[3];

    if (opcode != CMD_VERIFY || len != 5) {
        send_response(ctx, PROTO_NACK);
        return;
    }

    uint32_t host_crc = (uint32_t)ctx->rx_buf[4]
                      | ((uint32_t)ctx->rx_buf[5] << 8)
                      | ((uint32_t)ctx->rx_buf[6] << 16)
                      | ((uint32_t)ctx->rx_buf[7] << 24);

    if (host_crc == ctx->fw_crc32) {
        send_response(ctx, PROTO_ACK);
        ctx->state = STATE_READY;
    } else {
        send_response(ctx, PROTO_NACK);
        ctx->last_error = 3;
        ctx->state = STATE_ERROR;
    }
}

/* READY：等待 CMD_GO 或 CMD_RESET */
static void state_ready(loader_ctx_t *ctx)
{
    if (!receive_frame(ctx)) {
        send_response(ctx, PROTO_NACK);
        return;
    }

    uint8_t opcode = ctx->rx_buf[3];

    if (opcode == CMD_GO) {
        send_response(ctx, PROTO_ACK);
        ctx->state = STATE_JUMP;
    } else if (opcode == CMD_RESET) {
        send_response(ctx, PROTO_ACK);
        if (ctx->port.reset) {
            ctx->port.reset();
        }
        /* NVIC 软件复位 */
        *((volatile uint32_t *)0xE000ED0C) = 0x05FA0004;
        while (1) {}
    } else {
        send_response(ctx, PROTO_NACK);
    }
}

/* ============================================================
 *  公共 API
 * ============================================================ */

bool loader_port_init(loader_port_t *port)
{
    if (port == NULL) return false;
    if (port->is_boot_state == NULL) return false;
    if (port->flash_erase_page == NULL) return false;
    if (port->flash_write == NULL) return false;
    if (port->uart_rx == NULL) return false;
    if (port->uart_tx == NULL) return false;
    if (port->delay_ms == NULL) return false;
    /* port->reset 是可选的 */
    return true;
}

void boot_process(loader_port_t *port)
{
    loader_ctx_t ctx;

    /* 初始化状态 */
    ctx.state       = STATE_IDLE;
    ctx.fw_size     = 0;
    ctx.fw_received = 0;
    ctx.fw_crc32    = 0xFFFFFFFF;
    ctx.expected_seq = 0;
    ctx.last_error  = 0;
    ctx.port        = *port;

    /* 检查是否进入 bootloader 模式 */
    if (!ctx.port.is_boot_state()) {
        /* 不进入 bootloader，直接跳转到应用程序 */
        jump_to_app(CONF_APP_START_ADDR);
        return;
    }

    /* 主循环 */
    while (ctx.state != STATE_JUMP && ctx.state != STATE_ERROR) {
        switch (ctx.state) {
        case STATE_IDLE:    state_idle(&ctx);    break;
        case STATE_ERASE:   state_erase(&ctx);   break;
        case STATE_RECV:    state_recv(&ctx);     break;
        case STATE_VERIFY:  state_verify(&ctx);   break;
        case STATE_READY:   state_ready(&ctx);    break;
        default:            ctx.state = STATE_ERROR; break;
        }
    }

    if (ctx.state == STATE_JUMP) {
        jump_to_app(CONF_APP_START_ADDR);
    }

    /* STATE_ERROR：停留在 bootloader 不跳转，等待复位 */
    while (1) {}
}
