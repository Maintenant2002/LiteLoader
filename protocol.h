#ifndef LITE_LOADER_PROTOCOL_H
#define LITE_LOADER_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "port.h"

/*
 * loader_protocol_t - 传输协议抽象接口
 *
 * 实现者根据具体协议（YMODEM、XMODEM、自定义等）填充函数指针。
 * boot_process 通过此接口收发数据，与具体协议格式完全解耦。
 *
 * receive_chunk 约定：
 *   - 返回 true, *actual_len > 0  → 成功接收一个数据块
 *   - 返回 true, *actual_len == 0 → 传输结束（所有数据已接收）
 *   - 返回 false                  → 超时或不可恢复错误
 */
typedef struct {
    /* 协议名称（用于调试/日志，可为 NULL） */
    const char *name;

    /* 协议握手/初始化（如自定义协议等待 CMD_START，YMODEM 发送 'C'）
     * 返回 true 表示握手成功，false 表示失败/超时 */
    bool (*init)(loader_port_t *port);

    /* 接收一个固件数据块
     *   port       - 硬件抽象层
     *   buf        - 接收缓冲区（由调用者提供）
     *   max_len    - 缓冲区最大容量
     *   actual_len - [out] 实际接收到的字节数
     * 返回 true 且 *actual_len == 0 表示传输结束 */
    bool (*receive_chunk)(loader_port_t *port,
                          uint8_t *buf,
                          uint32_t max_len,
                          uint32_t *actual_len);

    /* 可选：整体校验（如 CRC32），NULL 表示跳过校验
     * 返回 true 表示校验通过 */
    bool (*verify)(loader_port_t *port);

    /* 可选：传输结束通知（如 YMODEM 发送 EOT），NULL 表示无操作 */
    void (*finish)(loader_port_t *port);
} loader_protocol_t;

#endif /* LITE_LOADER_PROTOCOL_H */
