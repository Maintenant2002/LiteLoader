# LiteLoader 测试基础设施

## 1. 目标

在 PC（Windows）上验证 bootloader 的完整传输流程，无需连接真实硬件。
Python 生成 XMODEM-CRC 帧文件，C 程序从文件读取字节模拟 UART 输入，
Flash 操作改为文件写入和控制台输出。

---

## 2. 文件结构

```
LiteLoader/
├── CMakeLists.txt              # 组件级 CMake
├── boot_loader.h               # 状态模式核心：boot_context_t, state_t
├── states.c                    # 状态实现 + boot_handle/boot_init
├── hal_port.h                  # 硬件抽象层接口
├── proto.h                     # 协议抽象接口
├── boot_config.h               # 配置（地址、页大小等）
├── proto_xmodem.c              # XMODEM-CRC 协议实现
├── test/
│   ├── CMakeLists.txt          # 测试构建入口
│   ├── gen_firmware.py         # Python XMODEM-CRC 帧生成器
│   ├── main_test.c             # 测试 main
│   └── port_win32.c            # Windows hal_port_t 实现
└── docs/
    └── test_infra.md           # 本文档
```

---

## 3. 状态模式架构

bootloader 采用状态模式重构，每个状态是一个函数：

```
CHECK_BOOT → PROTO_INIT → RECEIVE(循环) → VERIFY → 跳转
                           ↓ 任意状态失败
                          ERROR → 复位/终止
```

核心接口：

```c
boot_context_t ctx;
boot_init(&ctx, &state_check_boot_state, &hal, &proto_xmodem,
          0x08001000, 1024, 63);
boot_handle(&ctx);  // 阻塞运行直到完成或错误
```

---

## 4. XMODEM-CRC 协议

### 4.1 帧格式

```
SOH(0x01) | PKT(1B) | ~PKT(1B) | DATA[128] | CRC_HI | CRC_LO
```

- CRC-16/CCITT：多项式 0x1021，初始值 0x0000
- 包序号 1~255 循环

### 4.2 传输流程

```
Bootloader → Host:  'C' (0x43, 请求 CRC 模式)
Host → Bootloader:  SOH | 1 | ~1 | data[128] | crc16
Bootloader → Host:  ACK (0x06)
Host → Bootloader:  SOH | 2 | ~2 | data[128] | crc16
... (重复直到所有数据发送完毕)
Host → Bootloader:  EOT (0x04)
Bootloader → Host:  ACK (0x06)
```

### 4.3 数据区中的控制字节

0x18 (CAN) 可能出现在固件数据中。CAN 检查只在 `WAIT_SOH` 状态执行，
读数据阶段不检查，避免误触发取消。

---

## 5. Python 固件生成器（gen_firmware.py）

### 5.1 用法

```bash
python gen_firmware.py                  # 默认 63KB 固件
python gen_firmware.py -s 32768         # 指定 32KB
python gen_firmware.py -o my_fw.bin     # 指定输出文件名
```

### 5.2 CRC-16 实现

```python
def crc16_xmodem(data):
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc
```

必须与 `proto_xmodem.c` 中的 `crc16_update` 一致。

### 5.3 不足 128 字节的尾包

用 0x1A (SUB) 填充到 128 字节，与标准 XMODEM 一致。

---

## 6. Windows 端口层（port_win32.c）

| hal_port_t 字段 | Windows 实现 | 说明 |
|-----------------|-------------|------|
| `check_boot_mode` | 始终返回 `true` | 测试模式 |
| `erase_flash_page` | `printf()` | 仅打印 |
| `write_flash` | `fwrite()` | 按地址偏移写文件 |
| `uart_receive_byte` | `fgetc()` | 逐字节读帧文件 |
| `uart_transmit` | `printf()` | 打印响应 |
| `delay_ms` | `Sleep()` | Windows API |
| `reset_system` | `NULL` | 可选 |
| `debug_printf` | `vprintf()` | 调试输出 |

---

## 7. CMake 构建系统

### 7.1 双层设计

```
CMakeLists.txt（根）      ← 组件级：lite_loader 静态库
test/CMakeLists.txt       ← 测试构建：引入父目录，链接 lite_loader
```

### 7.2 嵌入式使用

```cmake
add_subdirectory(LiteLoader)
target_compile_definitions(lite_loader PUBLIC USING_ARM_CHIP)
target_link_libraries(your_firmware.elf lite_loader)
```

### 7.3 测试构建

```bash
cd test
cmake -B build -G "MinGW Makefiles"
cmake --build build
build\lite_loader_test.exe build\test_firmware.bin build\flash_out.bin
```

MinGW 工具链路径在 `test/CMakeLists.txt` 中配置：
```cmake
set(CMAKE_C_COMPILER "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/gcc.exe")
```

### 7.4 CTest

```bash
cd test/build && ctest
```

---

## 8. 调试记录

### 8.1 状态函数名与变量名冲突

**现象**：`state_protocol_init` 同时是 `static void` 函数和 `state_t` 全局变量，
编译报 `redeclared as different kind of symbol`。

**修复**：handler 函数统一加 `handler_` 前缀：
```c
static void handler_protocol_init(boot_context_t *ctx) { ... }
state_t state_protocol_init = { .handler = handler_protocol_init, ... };
```

### 8.2 boot_loader.h 循环依赖

**现象**：`typedef void (*state_handler_t)(boot_context_t *ctx)` 在
`struct boot_context` 定义之前使用了 `boot_context_t`，编译报未知类型。

**修复**：用结构体标签代替 typedef 前向声明：
```c
struct boot_context;
typedef void (*state_handler_t)(struct boot_context *ctx);
typedef struct boot_context { ... } boot_context_t;
```

### 8.3 XMODEM CAN 误触发

**现象**：传输 3 个包后 bootloader 发送 CAN (0x18) 终止，第 4 个包数据区
恰好包含 0x18 字节。

**根因**：CAN 检查在主循环顶层，对所有字节（包括数据字节）生效。
固件数据中的 0x18 被误判为取消命令。

**修复**：CAN 检查移入 `XM_STATE_WAIT_SOH` 分支，仅在等待包头时检查。

### 8.4 proto_xmodem.c 使用 malloc

**现象**：嵌入式环境无堆或堆极小，`malloc(sizeof(xmodem_priv_t))` 不可靠。

**修复**：改用静态变量 `static xmodem_priv_t s_xmodem_priv`。

---

## 9. 完整测试流程

```
gen_firmware.py          lite_loader_test.exe         flash_out.bin
     │                        │                            │
     │  生成 XMODEM-CRC 帧    │  读帧→解包→写 Flash        │
     ├───────────────────────►├───────────────────────────►│
     │                        │                            │
     Python                   C 程序                       验证
```

### 9.1 运行结果（63KB 固件）

```
=== LiteLoader Test ===
Protocol: XMODEM-CRC
=======================
[uart tx] 43           ← bootloader 发送 'C'
[flash] Erase page ...  ← 擦除 63 页
[uart tx] 06           ← ACK ×504（每包一个）
[flash] Write ...       ← 写入 504 × 128 = 64512 字节
[boot] Verification passed, would jump to app.
=== Test Complete ===
```

验证：Flash 输出与原始固件逐字节一致。
