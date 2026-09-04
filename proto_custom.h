#ifndef LITE_LOADER_PROTO_CUSTOM_H
#define LITE_LOADER_PROTO_CUSTOM_H

#include "protocol.h"

/*
 * LiteLoader 自定义传输协议
 *
 * 帧格式：MAGIC(0xAA) | SEQ(1B) | LEN(1B) | DATA(0-128B) | CRC(XOR,1B)
 *
 * 命令（DATA 首字节为 opcode）：
 *   CMD_START  (0x01) - 开始传输，携带 4 字节固件大小
 *   CMD_DATA   (0x02) - 数据帧，后续为原始固件字节
 *   CMD_VERIFY (0x03) - 校验，携带 4 字节 CRC32
 *   CMD_GO     (0x04) - 跳转到应用程序
 *   CMD_RESET  (0x05) - 复位
 *
 * 响应（bootloader → host）：MAGIC(0xAA) | STATUS | CRC(XOR)
 *   ACK  (0x00) / NACK (0x01) / BUSY (0x02)
 */

#define PROTO_CUSTOM_MAGIC      0xAA
#define PROTO_CUSTOM_ACK        0x00
#define PROTO_CUSTOM_NACK       0x01
#define PROTO_CUSTOM_BUSY       0x02

#define CMD_START               0x01
#define CMD_DATA                0x02
#define CMD_VERIFY              0x03
#define CMD_GO                  0x04
#define CMD_RESET               0x05

/* 每帧数据载荷大小（不含 opcode） */
#define CUSTOM_PAYLOAD_SIZE     127

/* 返回自定义协议的 loader_protocol_t 实例 */
const loader_protocol_t *proto_custom_get(void);

#endif /* LITE_LOADER_PROTO_CUSTOM_H */
