#ifndef LITE_LOADER_H
#define LITE_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include "port.h"

/* ---- 协议常量 ---- */

#define PROTO_MAGIC             0xAA
#define PROTO_ACK               0x00
#define PROTO_NACK              0x01
#define PROTO_BUSY              0x02

#define CMD_START               0x01
#define CMD_DATA                0x02
#define CMD_VERIFY              0x03
#define CMD_GO                  0x04
#define CMD_RESET               0x05

/* 最大帧大小：magic(1) + seq(1) + len(1) + payload(128) + crc(1) = 132 */
#define FRAME_MAX_SIZE          132

/* ---- 状态枚举 ---- */

typedef enum {
    STATE_IDLE,      /* 等待 CMD_START */
    STATE_ERASE,     /* 擦除 Flash 页 */
    STATE_RECV,      /* 接收数据帧并写入 Flash */
    STATE_VERIFY,    /* 等待 CMD_VERIFY 校验 CRC */
    STATE_READY,     /* 校验通过，等待 CMD_GO */
    STATE_JUMP,      /* 跳转到应用程序 */
    STATE_ERROR      /* 不可恢复错误 */
} loader_state_t;

/* ---- 加载器上下文（所有可变状态） ---- */

typedef struct {
    loader_state_t state;
    loader_port_t  port;

    /* 传输跟踪 */
    uint32_t fw_size;           /* 固件总大小（来自 CMD_START） */
    uint32_t fw_received;       /* 已接收字节数 */
    uint32_t fw_crc32;          /* 累计 CRC32 */
    uint8_t  expected_seq;      /* 下一个期望的序列号 */

    /* 接收缓冲区 */
    uint8_t  rx_buf[FRAME_MAX_SIZE];

    /* 错误信息 */
    uint8_t  last_error;
} loader_ctx_t;

#endif /* LITE_LOADER_H */
