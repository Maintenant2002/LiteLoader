# LiteLoader 测试基础设施

## 1. 目标

在 PC（Windows）上验证 bootloader 的完整传输流程，无需连接真实硬件。
核心思路：用 Python 生成符合自定义协议的帧数据文件，C 程序从文件读取字节模拟 UART 输入，
Flash 操作改为文件写入和控制台输出，在 PC 上跑通整个状态机。

---

## 2. 文件结构

```
LiteLoader/
├── CMakeLists.txt              # 组件级 CMake（嵌入式项目引用）
├── conf.h / loader.c / ...     # 核心库
├── test/
│   ├── CMakeLists.txt          # 测试构建入口（含 MinGW 工具链）
│   ├── gen_firmware.py         # Python 固件帧生成器
│   ├── main_test.c             # 测试 main
│   └── port_win32.c            # Windows 端口实现
└── docs/
    └── test_infra.md           # 本文档
```

---

## 3. Python 固件生成器（gen_firmware.py）

### 3.1 设计

生成的 `.bin` 文件不是原始固件，而是**按自定义协议封装好的帧序列**，
可以直接当作 UART 数据流喂给 bootloader。

```
文件内容：CMD_START帧 | CMD_DATA帧×N | CMD_VERIFY帧 | CMD_GO帧
```

### 3.2 CRC32 一致性

Python 端的 CRC32 实现必须与 `proto_custom.c` 完全一致，否则校验会失败。
两端都使用 nibble 查表法（16 项），初始值 `0xFFFFFFFF`，最终异或取反。

```python
def crc32_update(crc, data):
    TABLE = [0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
             0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
             0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
             0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C]
    for byte in data:
        crc = TABLE[(crc ^ byte) & 0x0F] ^ (crc >> 4)
        crc = TABLE[(crc ^ (byte >> 4)) & 0x0F] ^ (crc >> 4)
    return crc
```

### 3.3 帧格式

每个帧：`MAGIC(0xAA) | SEQ(1B) | LEN(1B) | DATA(LEN字节) | CRC(XOR,1B)`

- LEN 包含 opcode 在内
- CRC = XOR(SEQ, LEN, DATA[0..LEN-1])
- SEQ 是 `uint8_t`，超过 255 自然溢出归零

### 3.4 序列号溢出处理

默认固件大小 64512 字节，每帧载荷 127 字节，需要 508 个数据帧。
加上 CMD_START(seq=0)，总序列号可达 508+，远超 `uint8_t` 范围。

协议中 `uint8_t` 自然溢出，`s_expected_seq++` 从 255 回到 0。
Python 端用 `seq & 0xFF` 取模保持一致：

```python
def build_frame(seq, payload):
    seq_byte = seq & 0xFF  # 与 C 端 uint8_t 溢出行为一致
    ...
```

### 3.5 用法

```bash
python gen_firmware.py                  # 默认 63KB 固件
python gen_firmware.py -s 32768         # 指定 32KB
python gen_firmware.py -o my_fw.bin     # 指定输出文件名
```

---

## 4. Windows 端口层（port_win32.c）

将 `loader_port_t` 的每个函数指针映射到 PC 环境：

| 函数指针 | Windows 实现 | 说明 |
|----------|-------------|------|
| `is_boot_state` | 始终返回 `true` | 测试模式下强制进入 bootloader |
| `flash_erase_page` | `printf("[flash] 擦除页 0x%08X\n")` | 仅打印，不操作真实 Flash |
| `flash_write` | `fwrite()` 写入文件 | 按地址偏移写入输出 bin 文件 |
| `uart_rx` | `fgetc()` 读文件 | 逐字节从固件帧文件读取 |
| `uart_tx` | `printf()` 打印 | 打印响应字节（ACK/NACK） |
| `delay_ms` | `Sleep()` / `usleep()` | Windows 用 `Sleep()`，Linux 用 `usleep()` |
| `test` | `printf()` | 调试输出 |

### 4.1 Flash 输出文件

`flash_write` 将数据按地址偏移写入文件，偏移 = `addr - CONF_APP_START_ADDR`。
测试结束后可以用 Python 对比输出文件与原始固件是否一致。

### 4.2 UART 输入文件

`uart_rx` 用 `fgetc()` 从文件逐字节读取。文件用 `"rb"` 模式打开（二进制）。
读到 EOF 时返回 `false`，模拟串口超时。

---

## 5. CMake 构建系统

### 5.1 双层设计

```
CMakeLists.txt（根目录）      ← 组件级，提供 lite_loader 静态库
test/CMakeLists.txt           ← 测试构建入口，引入父目录的库
```

**组件级 CMake**（根目录）：

```cmake
add_library(lite_loader STATIC loader.c proto_custom.c)
target_include_directories(lite_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

嵌入式项目使用方式：
```cmake
add_subdirectory(LiteLoader)
target_compile_definitions(lite_loader PUBLIC USING_ARM_CHIP)
target_link_libraries(your_firmware.elf lite_loader)
```

**测试级 CMake**（test/）：

```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. lite_loader_build)
add_executable(lite_loader_test main_test.c port_win32.c)
target_link_libraries(lite_loader_test PRIVATE lite_loader)
```

### 5.2 MinGW 工具链

在 `project()` 之前设置编译器路径：

```cmake
set(CMAKE_C_COMPILER   "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/g++.exe")
set(CMAKE_RC_COMPILER  "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/windres.exe")
```

构建命令：
```bash
cd test
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

### 5.3 gen_firmware 自动目标

CMake 自动检测 Python3，找到后将 `gen_firmware.py` 注册为自定义命令，
构建时自动生成 `test_firmware.bin`：

```cmake
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/gen_firmware.py
                -o ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_firmware.py
    )
    add_custom_target(gen_firmware ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
    )
    add_dependencies(lite_loader_test gen_firmware)
endif()
```

### 5.4 CTest 集成

```cmake
enable_testing()
add_test(
    NAME    boot_process_test
    COMMAND lite_loader_test
            ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
            ${CMAKE_CURRENT_BINARY_DIR}/flash_out.bin
)
```

运行测试：`cd build && ctest`

---

## 6. 调试过程中发现并修复的 Bug

### 6.1 custom_receive_chunk 未返回 actual_len=0（严重）

**现象**：bootloader 收完所有数据帧后，VERIFY 和 GO 命令永远不被处理，
输出大量 NACK，最终超时或死循环。

**根因**：`custom_receive_chunk` 在收到最后一块数据后返回 `actual_len=数据长度`（如 8），
而非协议约定的 `actual_len=0`（传输结束信号）。
`boot_process` 的状态机依赖 `actual_len==0` 才进入 STATE_VERIFY，
所以永远卡在 STATE_RECV 循环里。

下一次调用时，函数去读 VERIFY 帧，发现 opcode 不是 CMD_DATA，发 NACK 继续循环。
然后读 GO 帧，同样 NACK。最后 EOF，返回 false → STATE_ERROR。

**修复**：在函数入口加判断，如果上一轮已收齐数据，直接返回 `actual_len=0`：

```c
static bool custom_receive_chunk(...) {
    if (s_fw_received >= s_fw_size) {
        *actual_len = 0;
        return true;   // 传输结束
    }
    // ... 正常接收逻辑
}
```

**教训**：`receive_chunk` 的返回值语义（`actual_len==0` 表示结束）是协议接口的核心约定，
实现者必须严格遵守。这个 bug 在没有端到端测试时很难发现。

### 6.2 ARM 内联汇编在非 ARM 平台编译失败

**现象**：`loader.c` 在 Windows/Linux x86 上编译报错 `no such instruction: msr msp`。

**根因**：`__set_MSP` 和 `jump_to_app` 包含 ARM 内联汇编（`MSR msp`、`cpsid i`），
虽然调用点在 `#ifdef USING_ARM_CHIP` 内，但函数定义本身没有被条件编译保护，
GCC 仍然尝试汇编这些指令。

**修复**：将 `__set_MSP` 和 `jump_to_app` 整体包进 `#ifdef USING_ARM_CHIP`：

```c
#ifdef USING_ARM_CHIP
static inline void __set_MSP(uint32_t topOfMainStack) { ... }
static void jump_to_app(uint32_t app_addr) { ... }
#endif
```

### 6.3 port.h 声明与 loader.h 定义不一致

**现象**：`port.h` 声明 `void boot_process(loader_port_t *port)`（1 参数），
`loader.h` 定义为 4 参数版本。C 语言允许不同翻译单元声明不一致，
运行时参数错位导致未定义行为。

**修复**：从 `port.h` 删除过时的 `boot_process` 声明，仅保留 `loader.h` 中的正确版本。

### 6.4 loader.c 缺少 `#include <stddef.h>`

**现象**：在标准 GCC 下编译报 `NULL undeclared`。ARM 工具链隐式包含了这个头文件，
标准 GCC 不会。

**修复**：添加 `#include <stddef.h>`。

---

## 7. 完整测试流程

```
┌──────────────┐     ┌──────────────────┐     ┌────────────────┐
│ gen_firmware │     │ lite_loader_test │     │  flash_out.bin │
│  .py         │────▶│  .exe            │────▶│  （验证用）     │
│ 生成帧文件    │     │ 读帧→解帧→写Flash │     │                │
└──────────────┘     └──────────────────┘     └────────────────┘
     Python               C 程序                   二进制文件
```

1. Python 生成随机固件，封装为协议帧文件
2. C 程序逐字节读取帧文件（模拟 UART），走完 bootloader 状态机
3. Flash 写入操作输出到文件
4. Python 验证输出文件与原始固件数据是否一致

### 7.1 运行结果示例

```
=== LiteLoader 测试开始 ===
固件输入: test_firmware.bin
Flash 输出: flash_out.bin
协议: custom
===========================

[uart tx] AA 00 AA           ← CMD_START ACK
[flash] 擦除页 0x08001000    ← 63 页擦除
...
[flash] 写入 0x08001000  127 字节   ← 数据块写入 Flash
[uart tx] AA 00 AA                  ← 每块 ACK
...
[uart tx] AA 00 AA           ← CMD_VERIFY ACK
[uart tx] AA 00 AA           ← CMD_GO ACK
[test] Boot complete, ready to jump to app.

=== 测试完成 ===
```

验证输出：`Data match: True`（Flash 输出与原始固件逐字节一致）
