# LiteLoader 开发过程文档

## 1. 项目目标

开发一个**不依赖硬件**的极简 bootloader 模块，通过 UART 接收固件并写入 Flash。
所有硬件相关代码通过函数指针抽象，使用者只需实现 port 层即可移植到任意 STM32 平台。

---

## 2. 第一阶段：基础架构搭建

### 2.1 硬件抽象层（port.h）

**设计原则**：bootloader 是一个"数据搬运工"——从 UART 读字节、校验、写入 Flash。凡不直接服务于这一目的的代码，都属于 port 层。

```c
typedef struct {
    bool (*is_boot_state)(void);        // 是否进入 bootloader（如读 GPIO）
    bool (*flash_erase_page)(uint32_t page_addr);
    bool (*flash_write)(uint32_t addr, const uint8_t *data, uint32_t size);
    bool (*uart_rx)(uint8_t *byte, uint32_t timeout_ms);
    bool (*uart_tx)(const uint8_t *data, uint32_t size);
    void (*delay_ms)(uint32_t ms);
    void (*reset)(void);                // 可选，NULL 时用 NVIC 软件复位
} loader_port_t;
```

**关键设计**：
- `reset` 是可选的（NULL-safe），其余 6 个必须非空
- `flash_write` 接收 `const uint8_t *`，bootloader 只读数据不修改
- `uart_rx` 带超时参数，避免死等

### 2.2 自定义传输协议

**为什么不用 YMODEM/XMODEM？**
这些标准协议的帧解析逻辑（1K 块、CRC-16、握手协商）会让代码膨胀，超出 4KB 目标。
自定义协议足够简单，适合极致精简场景。

**帧格式**：
```
MAGIC(0xAA) | SEQ(1B) | LEN(1B) | DATA(0-128B) | CRC(XOR,1B)
```

- 固定 MAGIC 字节便于帧同步
- LEN 字段让接收方精确知道要读多少字节，无需字节填充或超时判断
- 序列号防止重复帧（host 重传 ACK 丢失时的保护）
- 帧级 XOR 校验（1 字节，极低开销）+ 整体 CRC32 双层校验

**命令流**：
```
CMD_START(size) → ACK → CMD_DATA×N → ACK each → CMD_VERIFY(crc32) → ACK → CMD_GO → ACK → 跳转
```

### 2.3 状态机设计

```
is_boot_state? --false--> 直接跳转应用
       |
      true
       v
     IDLE → ERASE → RECV(循环) → VERIFY → READY → JUMP
```

- 所有状态放在一个 `switch` 主循环里
- 错误处理极简：帧错误发 NACK，重传由 host 负责
- bootloader 端零重试逻辑，把复杂度留给上位机

### 2.4 CRC32 实现选择

| 方案 | Flash 占用 | 速度 |
|------|-----------|------|
| 256 项查表 | ~1120B | 最快 |
| **16 项 nibble 查表** | **~104B** | 较快 |
| 逐位计算 | ~30B | 最慢 |

选择 nibble 查表——64B const + 40B 代码，速度与完整查表相差不大。

### 2.5 内存预算

```
RAM：  ~178B（loader_ctx_t 放栈上，含 132B rx_buf）
Flash：~2.4KB（状态机 + CRC + 帧解析）
```

---

## 3. 第二阶段：协议层抽象

### 3.1 问题

自定义协议写死了帧格式和命令码。如果想用 YMODEM 或其他协议，必须改 loader.c 核心代码——违反开闭原则。

### 3.2 解决方案：loader_protocol_t 接口

类似 `loader_port_t` 的思路，将协议操作抽象为函数指针表：

```c
typedef struct {
    const char *name;                              // 协议名称（调试用）
    bool (*init)(loader_port_t *port);             // 握手/初始化
    bool (*receive_chunk)(loader_port_t *port,     // 接收一个数据块
                          uint8_t *buf,
                          uint32_t max_len,
                          uint32_t *actual_len);
    bool (*verify)(loader_port_t *port);           // 整体校验，NULL=跳过
    void (*finish)(loader_port_t *port);           // 传输结束通知，NULL=空操作
} loader_protocol_t;
```

**核心约定**（`receive_chunk` 返回值语义）：
| 返回值 | actual_len | 含义 |
|--------|-----------|------|
| true | > 0 | 成功接收一个数据块 |
| true | 0 | 传输结束，所有数据已接收 |
| false | - | 超时或不可恢复错误 |

### 3.3 重构前后对比

**重构前** loader.c（~337 行，协议与状态机耦合）：
```
帧解析 + CRC32 + XOR + 命令处理 + 状态机 + 跳转逻辑
```

**重构后** 拆分为：
```
loader.c        ~110 行  纯协议无关状态机
proto_custom.c  ~230 行  自定义协议实现（帧解析/CRC/命令）
protocol.h       ~40 行  接口定义
```

### 3.4 状态机简化

```c
// 重构后的状态机核心（伪代码）
switch (ctx.state) {
    case IDLE:
        proto->init(port) ? ERASE : ERROR;
    case ERASE:
        擦除所有页 ? RECV : ERROR;
    case RECV:
        proto->receive_chunk(port, buf, ...);
        actual_len == 0 → VERIFY / JUMP;
        actual_len > 0  → flash_write;
        返回 false      → ERROR;
    case VERIFY:
        proto->verify ? proto->verify(port) : JUMP;
}
proto->finish(port);  // 传输结束通知
jump_to_app();
```

### 3.5 boot_process 签名变更

```c
// 原：void boot_process(loader_port_t *port);
// 新：
void boot_process(loader_port_t *port,
                  const loader_protocol_t *proto,
                  uint8_t *buf,        // 接收缓冲区（调用者提供）
                  uint32_t buf_size);
```

缓冲区由调用者提供（栈或静态分配），loader.c 不关心大小，由协议实现负责边界检查。

---

## 4. 架构总览

```
┌─────────────────────────────────────────────┐
│              用户应用 (main.c)               │
│  loader_port_t port = {...};                │
│  const loader_protocol_t *p = proto_custom_get(); │
│  uint8_t buf[256];                          │
│  boot_process(&port, p, buf, sizeof(buf));  │
└──────────────────┬──────────────────────────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
  ┌──────────┐         ┌─────────────┐
  │ loader.c │         │ port_xxx.c  │
  │ 状态机    │◄────────│ 硬件实现     │
  │ ~110 行   │         │ Flash/UART  │
  └────┬─────┘         └─────────────┘
       │
       ▼
  ┌──────────────┐    ┌──────────────┐
  │proto_custom.c│ 或 │proto_ymodem.c│  ← 用户选择
  │自定义协议     │    │YMODEM 实现    │
  └──────────────┘    └──────────────┘
```

---

## 5. 移植指南

### 5.1 移植到新平台（如 STM32F4）

1. 修改 `conf.h`：调整 `APP_START_ADDR`、`PAGE_COUNT`、`FLASH_PAGE_SIZE`
2. 实现 `port_stm32f4.c`：填充 `loader_port_t` 的 7 个函数指针
3. 选择协议：使用 `proto_custom_get()` 或自行实现 YMODEM 等

### 5.2 实现新协议（如 YMODEM）

```c
// proto_ymodem.c
static bool ymodem_init(loader_port_t *port) {
    // 发送 'C' 启动 YMODEM，等待 SOH 包（packet 0）
    // 从 packet 0 解析固件大小
}

static bool ymodem_receive_chunk(loader_port_t *port,
                                  uint8_t *buf,
                                  uint32_t max_len,
                                  uint32_t *actual_len) {
    // 等待 SOH(128B) 或 STX(1KB) 包
    // 校验 CRC-16 或 checksum
    // 发送 ACK
    // 收到 EOT 时返回 true + *actual_len = 0
}

static void ymodem_finish(loader_port_t *port) {
    // 发送 EOT + NAK + EOT
    // 等待最终 ACK
}

static const loader_protocol_t s_ymodem = {
    .name          = "ymodem",
    .init          = ymodem_init,
    .receive_chunk = ymodem_receive_chunk,
    .verify        = NULL,   // YMODEM 自带 CRC-16，无需额外校验
    .finish        = ymodem_finish,
};
```

---

## 6. 关键设计决策总结

| 决策 | 选择 | 原因 |
|------|------|------|
| 协议格式 | 自定义（非 YMODEM） | 代码量小，适合 < 4KB 目标 |
| 帧校验 | XOR（帧级）+ CRC32（整体） | 双层校验，XOR 极低开销 |
| CRC32 实现 | nibble 查表（16 项） | 104B Flash，速度接近完整查表 |
| 重传机制 | 由 host 负责 | bootloader 端零重试，保持精简 |
| 状态存储 | 栈上 loader_ctx_t | 避免全局变量，~178B RAM |
| 协议抽象 | loader_protocol_t 函数指针 | 开闭原则，新增协议不改状态机 |
| 接收缓冲区 | 调用者提供 | loader.c 不关心分配方式 |

---

## 7. 第三阶段：状态模式重构

### 7.1 问题

第二阶段的 `switch` 状态机虽然已经协议解耦，但仍有局限：
- 所有状态挤在一个函数里，添加新状态需要修改 `switch`
- 状态转换逻辑散布在 `case` 分支中，不易跟踪
- 配置（地址、页数）硬编码在 `conf.h`，运行时不可变

### 7.2 状态模式设计

将每个状态封装为独立函数，通过函数指针表驱动：

```c
typedef void (*state_handler_t)(struct boot_context *ctx);

typedef struct {
    state_handler_t handler;
    const char *name;
} state_t;

typedef struct boot_context {
    state_t *cur_state;
    hal_port_t *hal;
    proto_t *proto;
    void *proto_priv;           // 协议私有数据
    uint32_t app_start_addr;    // 运行时配置
    uint32_t flash_page_size;
    uint32_t total_pages;
    uint32_t current_write_addr;
} boot_context_t;
```

状态转换通过 `change_state(ctx, &state_xxx)` 实现，`boot_handle` 循环派发：

```c
void boot_handle(boot_context_t *ctx) {
    while (ctx->cur_state != &state_error) {
        ctx->cur_state->handler(ctx);
    }
    ctx->cur_state->handler(ctx);  // ERROR 处理一次
}
```

### 7.3 协议切换为 XMODEM-CRC

| 对比 | 旧（自定义协议） | 新（XMODEM-CRC） |
|------|-----------------|------------------|
| 帧格式 | MAGIC\|SEQ\|LEN\|DATA\|CRC | SOH\|PKT\|~PKT\|DATA[128]\|CRC16 |
| 校验 | XOR(帧级) + CRC32(整体) | CRC-16/CCITT(帧级) |
| 工具链 | 需要自定义 host 工具 | 标准协议，工具丰富 |
| 代码量 | ~230 行 | ~180 行 |

### 7.4 重构后文件结构

```
boot_loader.h     类型定义 + 接口声明
states.c          状态实现 + boot_handle/boot_init/change_state
hal_port.h        硬件抽象层接口
proto.h           协议抽象接口
boot_config.h     配置宏
proto_xmodem.c    XMODEM-CRC 协议实现
```

### 7.5 接口对比

**旧接口**：
```c
boot_process(&port, proto, buf, sizeof(buf));
```

**新接口**：
```c
boot_context_t ctx;
boot_init(&ctx, &state_check_boot_state, &hal, &proto_xmodem,
          APP_START_ADDR, FLASH_PAGE_SIZE, APP_PAGE_COUNT);
boot_handle(&ctx);
```
