#!/usr/bin/env python3
"""
gen_firmware.py - 生成 XMODEM-CRC 格式的测试固件

用法：
    python gen_firmware.py                  # 生成 63KB 固件
    python gen_firmware.py -s 32768         # 指定 32KB
    python gen_firmware.py -o my_fw.bin     # 指定输出文件名

生成的文件是完整的 XMODEM-CRC 传输数据流，可直接喂给 bootloader。
bootloader 会将解包后的原始数据写入 flash_out.bin。
"""

import argparse
import struct
import random
import os
import sys

# 必须与 boot_config.h 一致
APP_PAGE_COUNT  = 63
FLASH_PAGE_SIZE = 1024
FW_MAX_SIZE     = APP_PAGE_COUNT * FLASH_PAGE_SIZE  # 64512

# XMODEM 常量
SOH       = 0x01   # 128 字节数据包
EOT       = 0x04   # 传输结束
ACK       = 0x06   # 确认
NAK       = 0x15   # 否定确认
CAN       = 0x18   # 取消
CRC_MODE  = 0x43   # 'C' — CRC 模式

BLOCK_SIZE = 128   # XMODEM 标准块大小


def crc16_xmodem(data: bytes) -> int:
    """计算 XMODEM CRC-16（多项式 0x1021，初始值 0x0000）"""
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


def build_xmodem_packet(seq: int, data: bytes) -> bytes:
    """
    构造一个 XMODEM-CRC 数据包
    格式：SOH | PKT | ~PKT | DATA[128] | CRC_HI | CRC_LO
    """
    assert len(data) == BLOCK_SIZE, f"data must be {BLOCK_SIZE} bytes, got {len(data)}"
    assert 1 <= seq <= 255

    pkt = seq & 0xFF
    pkt_comp = (~pkt) & 0xFF
    crc = crc16_xmodem(data)
    crc_hi = (crc >> 8) & 0xFF
    crc_lo = crc & 0xFF

    return bytes([SOH, pkt, pkt_comp]) + data + bytes([crc_hi, crc_lo])


def main():
    parser = argparse.ArgumentParser(description="LiteLoader XMODEM-CRC 固件生成器")
    parser.add_argument("-s", "--size", type=int, default=FW_MAX_SIZE,
                        help=f"固件大小（字节，默认 {FW_MAX_SIZE}）")
    parser.add_argument("-o", "--output", default="test_firmware.bin",
                        help="输出文件名（默认 test_firmware.bin）")
    args = parser.parse_args()

    fw_size = args.size
    if fw_size == 0 or fw_size > FW_MAX_SIZE:
        print(f"错误：固件大小必须在 1 ~ {FW_MAX_SIZE} 之间", file=sys.stderr)
        sys.exit(1)

    # 1. 生成随机固件数据
    fw_data = bytes(random.getrandbits(8) for _ in range(fw_size))
    print(f"生成随机固件：{fw_size} 字节 ({fw_size / 1024:.1f} KB)")

    # 2. 分块构造 XMODEM 数据包
    output = bytearray()
    offset = 0
    seq = 1
    packet_count = 0

    while offset < fw_size:
        chunk = fw_data[offset:offset + BLOCK_SIZE]

        # 不足 128 字节的最后一个包用 0x1A (SUB) 填充
        if len(chunk) < BLOCK_SIZE:
            chunk = chunk + b'\x1a' * (BLOCK_SIZE - len(chunk))

        packet = build_xmodem_packet(seq, chunk)
        output.extend(packet)

        seq = (seq % 255) + 1  # 序号 1~255 循环
        offset += BLOCK_SIZE
        packet_count += 1

    # 3. EOT 结束
    output.append(EOT)

    print(f"XMODEM 包数：{packet_count}")
    print(f"总帧大小：{len(output)} 字节")

    # 4. 写入文件
    with open(args.output, "wb") as f:
        f.write(output)

    print(f"已写入：{args.output}")


if __name__ == "__main__":
    main()
