// proto_xmodem.c
#include "proto.h"
#include "hal_port.h"
#include "boot_loader.h"
#include "boot_config.h"
#include <string.h>

typedef enum {
    XM_STATE_WAIT_SOH,      // 等待 SOH (0x01)
    XM_STATE_WAIT_STX,      // 等待 STX (0x02) —— 本实现只支持 128 字节块
    XM_STATE_READ_DATA,
    XM_STATE_READ_CRC
} xmodem_state_t;

typedef struct {
    xmodem_state_t state;
    uint8_t packet_num;       // 期望的包序号（从 1 开始）
    uint8_t last_acked_seq;   // 上一个已确认的包序号（用于重复包检测）
    uint8_t recv_buf[128];    // 当前包数据
    uint32_t recv_len;        // 已接收字节数
    uint16_t recv_crc;        // 接收到的 CRC
    uint16_t calc_crc;        // 实时计算 CRC
    bool crc_mode;            // 使用 CRC16 (true) 还是校验和 (false)
    uint32_t retries;         // 重试计数
} xmodem_priv_t;

static uint16_t crc16_update(uint16_t crc, uint8_t data) {
    crc ^= (uint16_t)data << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
        else crc <<= 1;
    }
    return crc;
}

static xmodem_priv_t s_xmodem_priv;  /* 静态分配，避免 malloc */

static bool xmodem_init(void *ctx) {
    boot_context_t *boot = (boot_context_t*)ctx;
    hal_port_t *hal = boot->hal;

    /* 使用静态分配的私有数据 */
    memset(&s_xmodem_priv, 0, sizeof(s_xmodem_priv));
    s_xmodem_priv.crc_mode = true;
    s_xmodem_priv.packet_num = 1;
    boot->proto_priv = &s_xmodem_priv;

    // 发送 'C' (0x43) 启动 XMODEM CRC 模式
    uint8_t c = 0x43;
    hal->uart_transmit(&c, 1);

    // 等待第一个包（SOH）
    xmodem_priv_t *priv = (xmodem_priv_t*)boot->proto_priv;
    priv->state = XM_STATE_WAIT_SOH;
    return true;
}

static bool xmodem_receive_chunk(void *ctx, uint8_t *buf,
                                 uint32_t max_len, uint32_t *actual_len) {
    boot_context_t *boot = (boot_context_t*)ctx;
    hal_port_t *hal = boot->hal;
    xmodem_priv_t *priv = (xmodem_priv_t*)boot->proto_priv;
    uint8_t byte;

    while (1) {
        // 读取一个字节（带超时）
        if (!hal->uart_receive_byte(&byte, UART_TIMEOUT_MS)) {
            // 超时：重发 'C' 或 NAK
            if (priv->state == XM_STATE_WAIT_SOH) {
                hal->uart_transmit((uint8_t*)"\x43", 1); // 重发 'C'
            } else {
                hal->uart_transmit((uint8_t*)"\x15", 1); // 发送 NAK
            }
            priv->retries++;
            if (priv->retries > 10) return false; // 超时失败
            continue;
        }

        // 状态机
        switch (priv->state) {
            case XM_STATE_WAIT_SOH:
                if (byte == 0x18) { // CAN
                    hal->uart_transmit((uint8_t*)"\x18", 1);
                    return false;
                }
                if (byte == 0x01) { // SOH (128 字节块)
                    priv->state = XM_STATE_READ_DATA;
                    priv->recv_len = 0;
                    priv->calc_crc = 0x0000;
                    priv->recv_crc = 0;
                    priv->retries = 0;
                } else if (byte == 0x04) { // EOT
                    hal->uart_transmit((uint8_t*)"\x06", 1); // ACK
                    *actual_len = 0; // 表示传输结束
                    return true;
                } else {
                    // 其他字符，忽略，继续等待 SOH
                }
                break;

            case XM_STATE_READ_DATA: {
                // 接收包序号
                if (priv->recv_len == 0) {
                    priv->packet_num = byte; // 期望序号
                    priv->recv_len++;
                    break;
                }
                if (priv->recv_len == 1) {
                    // 补码序号校验
                    if ((priv->packet_num + byte) != 0xFF) {
                        // 序号错误，发送 NAK
                        hal->uart_transmit((uint8_t*)"\x15", 1);
                        priv->state = XM_STATE_WAIT_SOH; // 重新等待
                        priv->recv_len = 0;
                        break;
                    }
                    priv->recv_len++;
                    break;
                }
                // 接收数据（1..128 字节）
                if (priv->recv_len >= 2 && priv->recv_len < 130) {
                    priv->recv_buf[priv->recv_len - 2] = byte;
                    priv->calc_crc = crc16_update(priv->calc_crc, byte);
                    priv->recv_len++;
                    break;
                }
                // 接收 CRC（最后两字节）
                if (priv->recv_len == 130) {
                    priv->recv_crc = (uint16_t)byte << 8;
                    priv->recv_len++;
                    break;
                }
                if (priv->recv_len == 131) {
                    priv->recv_crc |= byte;
                    if (priv->recv_crc == priv->calc_crc) {
                        /* 重复包：ACK 丢失导致发送方重传，发 ACK 继续等下一包 */
                        if (priv->packet_num == priv->last_acked_seq) {
                            hal->uart_transmit((uint8_t*)"\x06", 1);
                            priv->state = XM_STATE_WAIT_SOH;
                            priv->recv_len = 0;
                            break;  /* 不返回，继续接收 */
                        }
                        /* 正常接收，发送 ACK */
                        hal->uart_transmit((uint8_t*)"\x06", 1);
                        uint32_t copy_len = (max_len < 128) ? max_len : 128;
                        memcpy(buf, priv->recv_buf, copy_len);
                        *actual_len = copy_len;
                        priv->last_acked_seq = priv->packet_num;
                        priv->state = XM_STATE_WAIT_SOH;
                        priv->packet_num++;
                        return true;
                    } else {
                        // CRC 错误，发送 NAK
                        hal->uart_transmit((uint8_t*)"\x15", 1);
                        priv->state = XM_STATE_WAIT_SOH;
                        priv->recv_len = 0;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

static bool xmodem_verify(void *ctx) {
    (void)ctx;
    return true;
}

static void xmodem_finish(void *ctx) {
    (void)ctx;
}

proto_t proto_xmodem = {
    .name = "XMODEM-CRC",
    .init = xmodem_init,
    .receive_chunk = xmodem_receive_chunk,
    .verify = xmodem_verify,
    .finish = xmodem_finish
};