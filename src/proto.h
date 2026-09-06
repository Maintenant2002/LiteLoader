#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include <stdbool.h>

/* 传输协议抽象接口 */
typedef struct {
    const char *name;

    /* 握手/初始化（如发送 'C' 启动 XMODEM CRC 模式）
     * 返回 true 表示握手成功 */
    bool (*init)(void *ctx);

    /* 接收一个数据块
     *   ctx        - boot_context_t*（通过 void* 传递）
     *   buf        - 接收缓冲区
     *   max_len    - 缓冲区大小
     *   actual_len - [out] 实际接收字节数
     * 返回 true 且 *actual_len == 0 表示传输结束（EOT 收到） */
    bool (*receive_chunk)(void *ctx, uint8_t *buf,
                          uint32_t max_len, uint32_t *actual_len);

    /* 整体校验（如 CRC32），可为 NULL */
    bool (*verify)(void *ctx);

    /* 结束通知，可为 NULL */
    void (*finish)(void *ctx);
} proto_t;

#endif /* PROTO_H */
