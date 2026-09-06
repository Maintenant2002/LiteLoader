# LiteLoader 开发文档

## 1. 项目目标

开发一个**不依赖硬件**的极简 bootloader 模块，通过 UART 接收固件并写入 Flash。
所有硬件相关代码通过函数指针抽象，使用者只需实现 HAL 层即可移植到任意 STM32 平台。

---

## 2. 架构设计

### 2.1 状态模式

bootloader 采用状态模式，每个状态是独立的函数，通过 `change_state` 驱动状态转换：

```
CHECK_BOOT → PROTO_INIT → RECEIVE(循环) → VERIFY → 跳转
                           ↓ 任意状态失败
                          ERROR → 复位
```

核心类型：

```c
typedef void (*state_handler_t)(struct boot_context *ctx);

typedef struct {
    state_handler_t handler;
    const char *name;
} state_t;

typedef struct {
    state_t *init_state;        /* 初始状态 */
    hal_port_t *hal;            /* 硬件抽象层 */
    proto_t *proto;             /* 传输协议 */
    uint8_t *rx_buf;            /* 接收缓冲区（调用者分配） */
    uint32_t rx_buf_size;       /* 缓冲区大小 */
    uint32_t app_start_addr;    /* 应用起始地址 */
    uint32_t flash_page_size;   /* Flash 页大小 */
    uint32_t total_pages;       /* 需擦除的页数 */
} boot_config_t;

typedef struct boot_context {
    state_t *cur_state;
    hal_port_t *hal;
    proto_t *proto;
    void *proto_priv;             /* 协议私有数据 */
    uint8_t *rx_buf;              /* 接收缓冲区指针 */
    uint32_t rx_buf_size;
    uint32_t app_start_addr;
    uint32_t flash_page_size;
    uint32_t total_pages;
    uint32_t current_write_addr;  /* 运行时更新 */
} boot_context_t;
```

`boot_handle` 循环派发当前状态的 handler，直到进入 ERROR 状态：

```c
void boot_handle(boot_context_t *ctx) {
    while (ctx->cur_state != &state_error) {
        ctx->cur_state->handler(ctx);
    }
    ctx->cur_state->handler(ctx);
}
```

### 2.2 硬件抽象层（hal_port.h）

所有硬件操作通过函数指针抽象，使用者填充 `hal_port_t` 即可移植：

```c
typedef struct {
    bool (*check_boot_mode)(void);          // 是否进入 bootloader
    bool (*erase_flash_page)(uint32_t page_addr);
    bool (*write_flash)(uint32_t addr, const uint8_t *data, uint32_t size);
    bool (*uart_receive_byte)(uint8_t *byte, uint32_t timeout_ms);
    bool (*uart_transmit)(const uint8_t *data, uint32_t size);
    void (*delay_ms)(uint32_t ms);
    void (*reset_system)(void);             // 可选，NULL 用 NVIC 复位
    void (*debug_printf)(const char *fmt, ...);  // 可选
} hal_port_t;
```

### 2.3 协议抽象层（proto.h）

协议操作通过 `proto_t` 函数指针表抽象，与状态机完全解耦：

```c
typedef struct {
    const char *name;
    bool (*init)(void *ctx);
    bool (*receive_chunk)(void *ctx, uint8_t *buf,
                          uint32_t max_len, uint32_t *actual_len);
    bool (*verify)(void *ctx);     // 可选，NULL 跳过
    void (*finish)(void *ctx);     // 可选，NULL 空操作
} proto_t;
```

`receive_chunk` 返回值语义：

| 返回值 | actual_len | 含义 |
|--------|-----------|------|
| true | > 0 | 成功接收一个数据块 |
| true | 0 | 传输结束 |
| false | - | 超时或不可恢复错误 |

### 2.4 XMODEM-CRC 协议实现（proto_xmodem.c）

使用标准 XMODEM-CRC 协议，CRC-16/CCITT（多项式 0x1021，初始值 0x0000）。

帧格式：`SOH(0x01) | PKT(1B) | ~PKT(1B) | DATA[128] | CRC_HI | CRC_LO`

传输流程：
```
Bootloader → Host:  'C' (0x43, 请求 CRC 模式)
Host → Bootloader:  SOH | 1 | ~1 | data[128] | crc16
Bootloader → Host:  ACK (0x06)
... (重复直到所有数据发送完毕)
Host → Bootloader:  EOT (0x04)
Bootloader → Host:  ACK (0x06)
```

---

## 3. 文件结构

```
LiteLoader/
├── src/
│   ├── boot_loader.h       核心类型：boot_context_t, state_t, state_handler_t
│   ├── states.c            状态实现 + boot_handle/boot_init/change_state
│   ├── hal_port.h          硬件抽象层接口
│   ├── proto.h             协议抽象接口
│   ├── boot_config.h       配置宏（地址、页大小、超时等）
│   └── proto_xmodem.c      XMODEM-CRC 协议实现
├── scripts/
│   └── flash.py            上位机烧录工具
├── test/
│   ├── gen_firmware.py     测试固件生成器（XMODEM-CRC 帧）
│   ├── main_test.c         Windows 测试入口
│   ├── port_win32.c        Windows hal_port_t 实现
│   └── CMakeLists.txt      测试构建
├── CMakeLists.txt          组件级 CMake
└── doc/
    ├── development_notes.md  本文档
    └── test_infra.md         测试基础设施文档
```

---

## 4. 移植指南

### 4.1 移植到新平台（如 STM32F4）

1. 修改 `boot_config.h`：调整 `APP_START_ADDR`、`FLASH_PAGE_SIZE`、`APP_PAGE_COUNT`
2. 实现 `port_stm32f4.c`：填充 `hal_port_t` 的函数指针
3. 选择协议：使用 `proto_xmodem` 或自行实现

### 4.2 使用方式

```c
#include "boot_loader.h"

extern proto_t proto_xmodem;

int main(void) {
    hal_port_t hal = { /* 填充你的硬件实现 */ };
    uint8_t rx_buf[128];

    boot_config_t cfg = {
        .init_state      = &state_check_boot_state,
        .hal             = &hal,
        .proto           = &proto_xmodem,
        .rx_buf          = rx_buf,
        .rx_buf_size     = sizeof(rx_buf),
        .app_start_addr  = 0x08001000,
        .flash_page_size = 1024,
        .total_pages     = 63,
    };

    boot_context_t ctx;
    boot_init(&ctx, &cfg);
    boot_handle(&ctx);  // 阻塞，不会返回（成功跳转或错误复位）
}
```

### 4.3 实现新协议

```c
// proto_custom.c
static bool my_init(void *ctx) { ... }
static bool my_receive_chunk(void *ctx, uint8_t *buf,
                              uint32_t max_len, uint32_t *actual_len) { ... }

proto_t proto_custom = {
    .name = "custom",
    .init = my_init,
    .receive_chunk = my_receive_chunk,
    .verify = NULL,
    .finish = NULL,
};
```

---

## 5. 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 状态管理 | 状态模式（函数指针） | 每个状态独立，易于扩展 |
| 协议格式 | XMODEM-CRC | 标准协议，工具链丰富 |
| 帧校验 | CRC-16/CCITT | XMODEM 标准，16 位查表 |
| 配置方式 | boot_config_t 初始化结构体 | 参数集中，易于扩展 |
| 状态存储 | 栈上 boot_context_t | 避免全局变量 |
| 接收缓冲区 | 调用者通过 rx_buf 提供 | 缓冲区大小由协议需求决定 |
| 协议抽象 | proto_t 函数指针 | 开闭原则，新增协议不改状态机 |
| 私有数据 | ctx->proto_priv | 协议内部状态不污染上下文 |
