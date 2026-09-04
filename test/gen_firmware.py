#!/usr/bin/env python3
"""
gen_firmware.py - 生成测试用随机固件 bin 文件

用法：
    python gen_firmware.py                  # 生成 63KB 随机固件
    python gen_firmware.py -s 32768         # 指定 32KB
    python gen_firmware.py -o my_fw.bin     # 指定输出文件名

生成文件同时包含协议帧数据（供 bootloader 通过串口接收），
bootloader 会将解帧后的原始数据写入 flash_out.bin。
"""

import argparse
import struct
import random
import sys
import os

# 必须与 conf.h 一致
APP_PAGE_COUNT  = 63
FLASH_PAGE_SIZE = 1024
FW_MAX_SIZE     = APP_PAGE_COUNT * FLASH_PAGE_SIZE  # 64512

# 必须与 proto_custom.h 一致
MAGIC           = 0xAA
CMD_START       = 0x01
CMD_DATA        = 0x02
CMD_VERIFY      = 0x03
CMD_GO          = 0x04
PAYLOAD_SIZE    = 127   # 每帧数据载荷（不含 opcode）


def xor_checksum(data: bytes) -> int:
    """XOR 校验"""
    s = 0
    for b in data:
        s ^= b
    return s & 0xFF


def crc32_update(crc: int, data: bytes) -> int:
    """CRC32 nibble 查表更新（与 proto_custom.c 一致）"""
    TABLE = [
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
        0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    ]
    for byte in data:
        crc = TABLE[(crc ^ byte) & 0x0F] ^ (crc >> 4)
        crc = TABLE[(crc ^ (byte >> 4)) & 0x0F] ^ (crc >> 4)
    return crc


def build_frame(seq: int, payload: bytes) -> bytes:
    """
    构造一个协议帧：MAGIC | SEQ | LEN | DATA | CRC
    LEN = len(payload)  (包含 opcode 在内)
    payload 已包含 opcode
    """
    assert len(payload) <= 1 + PAYLOAD_SIZE, f"payload too large: {len(payload)}"
    seq_byte = seq & 0xFF
    data_field = payload
    length = len(data_field)  # opcode + 实际数据
    crc = xor_checksum(struct.pack("BB", seq_byte, length) + data_field)
    return struct.pack("BBB", MAGIC, seq_byte, length) + data_field + struct.pack("B", crc)


def main():
    parser = argparse.ArgumentParser(description="LiteLoader 测试固件生成器")
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

    # 2. 计算 CRC32
    crc = 0xFFFFFFFF
    crc = crc32_update(crc, fw_data)
    crc ^= 0xFFFFFFFF
    print(f"CRC32: 0x{crc:08X}")

    # 3. 构造帧序列
    frames = bytearray()
    seq = 0

    # CMD_START: opcode(1B) + fw_size(4B LE) = 5 字节
    start_payload = struct.pack("<BI", CMD_START, fw_size)
    frames += build_frame(seq, start_payload)
    seq += 1

    # CMD_DATA × N
    offset = 0
    data_frame_count = 0
    while offset < fw_size:
        chunk = fw_data[offset:offset + PAYLOAD_SIZE]
        data_payload = bytes([CMD_DATA]) + chunk
        frames += build_frame(seq, data_payload)
        seq += 1
        offset += len(chunk)
        data_frame_count += 1

    # CMD_VERIFY: opcode(1B) + crc32(4B LE) = 5 字节
    verify_payload = struct.pack("<BI", CMD_VERIFY, crc)
    frames += build_frame(seq, verify_payload)
    seq += 1

    # CMD_GO
    go_payload = bytes([CMD_GO])
    frames += build_frame(seq, go_payload)

    print(f"帧统计：CMD_START=1, CMD_DATA={data_frame_count}, "
          f"CMD_VERIFY=1, CMD_GO=1, 共 {seq + 1} 帧")

    # 4. 写入文件
    output_path = args.output
    with open(output_path, "wb") as f:
        f.write(frames)

    file_size = os.path.getsize(output_path)
    print(f"已写入：{output_path}（{file_size} 字节）")


if __name__ == "__main__":
    main()
