#include "proto_custom.h"
#include "conf.h"

/* ============================================================
 *  内部状态（协议实例私有，通过静态变量保存在 init 和 receive_chunk 之间）
 * ============================================================ */

static uint32_t s_fw_size;          /* 固件总大小（来自 CMD_START） */
static uint32_t s_fw_received;      /* 已接收字节数 */
static uint32_t s_fw_crc32;         /* 累计 CRC32 */
static uint8_t  s_expected_seq;     /* 下一个期望的序列号 */

/* ============================================================
 *  CRC32 nibble 查表（16 项，64 字节 Flash）
 * ============================================================ */

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

/* ============================================================
 *  帧收发辅助
 * ============================================================ */

static uint8_t xor_checksum(const uint8_t *buf, uint32_t len)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum ^= buf[i];
    }
    return sum;
}

static void send_response(loader_port_t *port, uint8_t status)
{
    uint8_t resp[3];
    resp[0] = PROTO_CUSTOM_MAGIC;
    resp[1] = status;
    resp[2] = resp[0] ^ resp[1];
    port->uart_tx(resp, 3);
}

/* 接收一个完整帧到 buf
 * 帧格式：MAGIC | SEQ | LEN | DATA[LEN] | CRC
 * buf 至少需要 132 字节
 * 返回 true 表示收到有效帧 */
static bool receive_frame(loader_port_t *port, uint8_t *buf, uint32_t buf_size)
{
    (void)buf_size;  /* 调用者保证 buf >= 132 */

    /* 1. 等待 MAGIC 字节 */
    while (1) {
        if (!port->uart_rx(&buf[0], CONF_UART_TIMEOUT_MS)) {
            return false;
        }
        if (buf[0] == PROTO_CUSTOM_MAGIC) {
            break;
        }
    }

    /* 2. SEQ + LEN */
    if (!port->uart_rx(&buf[1], CONF_UART_TIMEOUT_MS)) return false;
    if (!port->uart_rx(&buf[2], CONF_UART_TIMEOUT_MS)) return false;

    uint8_t len = buf[2];

    /* 3. DATA */
    for (uint8_t i = 0; i < len; i++) {
        if (!port->uart_rx(&buf[3 + i], CONF_UART_TIMEOUT_MS)) {
            return false;
        }
    }

    /* 4. CRC */
    if (!port->uart_rx(&buf[3 + len], CONF_UART_TIMEOUT_MS)) {
        return false;
    }

    /* 5. 校验 XOR(seq, len, data) == crc */
    uint8_t calc_crc = xor_checksum(&buf[1], 2 + len);
    if (calc_crc != buf[3 + len]) {
        return false;
    }

    return true;
}

/* ============================================================
 *  协议接口实现
 * ============================================================ */

/* init：等待 CMD_START，解析固件大小，回复 ACK */
static bool custom_init(loader_port_t *port)
{
    /* 使用栈上的临时缓冲区接收 CMD_START 帧 */
    uint8_t frame[132];

    while (1) {
        if (!receive_frame(port, frame, sizeof(frame))) {
            send_response(port, PROTO_CUSTOM_NACK);
            continue;
        }

        uint8_t opcode = frame[3];
        if (opcode == CMD_START && frame[2] == 5) {
            uint32_t fw_size = (uint32_t)frame[4]
                             | ((uint32_t)frame[5] << 8)
                             | ((uint32_t)frame[6] << 16)
                             | ((uint32_t)frame[7] << 24);

            if (fw_size == 0 || fw_size > CONF_FW_MAX_SIZE) {
                send_response(port, PROTO_CUSTOM_NACK);
                continue;
            }

            s_fw_size      = fw_size;
            s_fw_received  = 0;
            s_fw_crc32     = 0xFFFFFFFF;
            s_expected_seq = 1;  /* CMD_START 已用 seq=0 */

            send_response(port, PROTO_CUSTOM_ACK);
            return true;
        }

        send_response(port, PROTO_CUSTOM_NACK);
    }
}

/* receive_chunk：接收 CMD_DATA 帧，返回固件数据块
 * 当 fw_received >= fw_size 时返回 true + actual_len=0 表示传输结束 */
static bool custom_receive_chunk(loader_port_t *port,
                                 uint8_t *buf,
                                 uint32_t max_len,
                                 uint32_t *actual_len)
{
    (void)buf;
    (void)max_len;

    /* 上一轮已收齐全部数据，通知调用者传输结束 */
    if (s_fw_received >= s_fw_size) {
        *actual_len = 0;
        return true;
    }

    uint8_t frame[132];

    while (1) {
        if (!receive_frame(port, frame, sizeof(frame))) {
            send_response(port, PROTO_CUSTOM_NACK);
            continue;
        }

        uint8_t seq    = frame[1];
        uint8_t len    = frame[2];
        uint8_t opcode = frame[3];

        if (opcode != CMD_DATA) {
            send_response(port, PROTO_CUSTOM_NACK);
            continue;
        }

        /* 序列号校验 */
        if (seq != s_expected_seq) {
            if (seq == (uint8_t)(s_expected_seq - 1)) {
                send_response(port, PROTO_CUSTOM_ACK);  /* 重复帧 */
            } else {
                send_response(port, PROTO_CUSTOM_NACK);
            }
            continue;
        }

        /* 固件数据：frame[4 .. 4+len-1]，len 包含 opcode，实际 = len - 1 */
        uint32_t data_len = len - 1;

        /* 防止缓冲区溢出 */
        if (data_len > max_len) {
            send_response(port, PROTO_CUSTOM_NACK);
            continue;
        }

        /* 复制到调用者缓冲区 */
        for (uint32_t i = 0; i < data_len; i++) {
            buf[i] = frame[4 + i];
        }

        /* 更新 CRC32 */
        s_fw_crc32 = crc32_update(s_fw_crc32, frame + 4, data_len);

        s_fw_received += data_len;
        s_expected_seq++;

        send_response(port, PROTO_CUSTOM_ACK);

        *actual_len = data_len;

        /* 检查是否已收齐 */
        if (s_fw_received >= s_fw_size) {
            s_fw_crc32 ^= 0xFFFFFFFF;  /* CRC32 最终异或 */
        }

        return true;
    }
}

/* verify：等待 CMD_VERIFY，校验 CRC32 */
static bool custom_verify(loader_port_t *port)
{
    uint8_t frame[132];

    while (1) {
        if (!receive_frame(port, frame, sizeof(frame))) {
            send_response(port, PROTO_CUSTOM_NACK);
            continue;
        }

        uint8_t opcode = frame[3];
        if (opcode != CMD_VERIFY || frame[2] != 5) {
            send_response(port, PROTO_CUSTOM_NACK);
            continue;
        }

        uint32_t host_crc = (uint32_t)frame[4]
                          | ((uint32_t)frame[5] << 8)
                          | ((uint32_t)frame[6] << 16)
                          | ((uint32_t)frame[7] << 24);

        if (host_crc == s_fw_crc32) {
            send_response(port, PROTO_CUSTOM_ACK);
            return true;
        }

        send_response(port, PROTO_CUSTOM_NACK);
        return false;
    }
}

/* finish：等待 CMD_GO 或 CMD_RESET */
static void custom_finish(loader_port_t *port)
{
    uint8_t frame[132];

    while (1) {
        if (!receive_frame(port, frame, sizeof(frame))) {
            send_response(port, PROTO_CUSTOM_NACK);
            continue;
        }

        uint8_t opcode = frame[3];

        if (opcode == CMD_GO) {
            send_response(port, PROTO_CUSTOM_ACK);
            return;
        }

        if (opcode == CMD_RESET) {
            send_response(port, PROTO_CUSTOM_ACK);
            if (port->reset) {
                port->reset();
            }
            /* NVIC 软件复位 */
            *((volatile uint32_t *)0xE000ED0C) = 0x05FA0004;
            while (1) {}
        }

        send_response(port, PROTO_CUSTOM_NACK);
    }
}

/* ============================================================
 *  协议实例
 * ============================================================ */

static const loader_protocol_t s_proto_custom = {
    .name          = "custom",
    .init          = custom_init,
    .receive_chunk = custom_receive_chunk,
    .verify        = custom_verify,
    .finish        = custom_finish,
};

const loader_protocol_t *proto_custom_get(void)
{
    return &s_proto_custom;
}
