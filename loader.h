#ifndef LITE_LOADER_H
#define LITE_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include "port.h"
#include "protocol.h"

/* ---- 加载器状态 ---- */

typedef enum {
    STATE_IDLE,      /* 协议握手/初始化 */
    STATE_ERASE,     /* 擦除 Flash 页 */
    STATE_RECV,      /* 循环接收数据块并写入 Flash */
    STATE_VERIFY,    /* 整体校验（可选） */
    STATE_JUMP,      /* 跳转到应用程序 */
    STATE_ERROR      /* 不可恢复错误 */
} loader_state_t;

/* ---- 加载器上下文 ---- */

typedef struct {
    loader_state_t state;
    loader_port_t  port;
    loader_protocol_t proto;

    /* 传输跟踪 */
    uint32_t fw_received;       /* 已接收字节数 */
    uint8_t  last_error;        /* 0 = 无错误 */

    /* 接收缓冲区（由 boot_process 栈分配，大小由调用者决定） */
    uint8_t  *rx_buf;
    uint32_t  rx_buf_size;
} loader_ctx_t;

/* bootloader 主入口
 *   port  - 硬件抽象层（已初始化）
 *   proto - 协议实现（已填充函数指针）
 *   buf   - 接收缓冲区（建议 >= 256 字节）
 *   buf_size - 缓冲区大小
 * 不会返回（除非跳转到应用程序或进入错误死循环） */
void boot_process(loader_port_t *port,
                  const loader_protocol_t *proto,
                  uint8_t *buf,
                  uint32_t buf_size);

#endif /* LITE_LOADER_H */
