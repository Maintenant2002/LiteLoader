#!/usr/bin/env python3
"""
LiteLoader 上位机烧录工具
用法：python flash.py <串口> <固件.bin> [波特率]
示例：python flash.py COM3 app.bin 115200
"""

import sys
import struct
import serial
import time
import zlib

# 协议常量（与 loader.h 保持一致）
PROTO_MAGIC = 0xAA
PROTO_ACK   = 0x00
PROTO_NACK  = 0x01
PROTO_BUSY  = 0x02

CMD_START   = 0x01
CMD_DATA    = 0x02
CMD_VERIFY  = 0x03
CMD_GO      = 0x04

DATA_PAYLOAD_SIZE = 127  # CUSTOM_PAYLOAD_SIZE (proto_custom.h)
DEFAULT_BAUD      = 115200
RECV_TIMEOUT      = 5.0  # 等待响应的超时（秒）


def xor_checksum(data: bytes) -> int:
    """计算 XOR 校验和"""
    result = 0
    for b in data:
        result ^= b
    return result & 0xFF


def build_frame(seq: int, data: bytes) -> bytes:
    """
    构建发送帧
    帧格式：MAGIC | SEQ | LEN | DATA | CRC
    """
    assert len(data) <= 128
    length = len(data)
    header = bytes([PROTO_MAGIC, seq & 0xFF, length])
    crc = xor_checksum(bytes([seq & 0xFF, length]) + data)
    return header + data + bytes([crc])


def send_frame(ser: serial.Serial, seq: int, data: bytes):
    """发送一帧并返回 True"""
    frame = build_frame(seq, data)
    ser.write(frame)
    ser.flush()


def recv_response(ser: serial.Serial) -> int:
    """
    接收 bootloader 响应（3 字节：MAGIC | STATUS | CRC）
    返回状态码，超时返回 None
    """
    start = time.time()
    buf = bytearray()

    while time.time() - start < RECV_TIMEOUT:
        if ser.in_waiting:
            buf.extend(ser.read(ser.in_waiting))

            # 等待完整的 3 字节响应
            while len(buf) >= 3:
                # 找到 MAGIC
                idx = buf.find(PROTO_MAGIC)
                if idx < 0:
                    buf.clear()
                    break
                if idx > 0:
                    del buf[:idx]  # 丢弃 MAGIC 之前的垃圾

                if len(buf) < 3:
                    break

                magic, status, crc = buf[0], buf[1], buf[2]
                expected_crc = xor_checksum(bytes([status]))
                del buf[:3]

                if magic == PROTO_MAGIC and crc == expected_crc:
                    return status
                # CRC 不匹配，继续找下一个 MAGIC
        else:
            time.sleep(0.001)

    return None


def wait_ack(ser: serial.Serial, desc: str = "") -> bool:
    """等待 ACK，打印状态"""
    status = recv_response(ser)
    if status == PROTO_ACK:
        return True
    elif status == PROTO_NACK:
        print(f"  [NACK] {desc}")
        return False
    elif status == PROTO_BUSY:
        print(f"  [BUSY] {desc}")
        return False
    else:
        print(f"  [超时] {desc}")
        return False


def flash(port: str, firmware_path: str, baudrate: int = DEFAULT_BAUD):
    """主烧录流程"""

    # 读取固件文件
    with open(firmware_path, "rb") as f:
        firmware = f.read()

    fw_size = len(firmware)
    fw_crc32 = zlib.crc32(firmware) & 0xFFFFFFFF
    print(f"固件文件：{firmware_path}")
    print(f"固件大小：{fw_size} 字节")
    print(f"CRC32：0x{fw_crc32:08X}")

    # 打开串口
    ser = serial.Serial(port, baudrate, timeout=0.1)
    ser.reset_input_buffer()
    time.sleep(0.1)

    try:
        # 1. CMD_START：发送固件大小
        print("\n[1/4] 发送 CMD_START...")
        start_data = bytes([CMD_START]) + struct.pack("<I", fw_size)
        send_frame(ser, 0, start_data)
        if not wait_ack(ser, "CMD_START 失败"):
            return False

        # 2. CMD_DATA：分帧发送固件数据
        total_frames = (fw_size + DATA_PAYLOAD_SIZE - 1) // DATA_PAYLOAD_SIZE
        print(f"\n[2/4] 发送固件数据（{total_frames} 帧）...")

        seq = 1  # 从 1 开始（0 已用于 CMD_START）
        offset = 0
        while offset < fw_size:
            chunk = firmware[offset:offset + DATA_PAYLOAD_SIZE]
            data = bytes([CMD_DATA]) + chunk
            send_frame(ser, seq & 0xFF, data)

            if not wait_ack(ser, f"帧 {seq} 失败（offset={offset}）"):
                return False

            offset += len(chunk)
            seq += 1

            # 进度显示
            progress = min(100, int(offset * 100 / fw_size))
            bar = "█" * (progress // 5) + "░" * (20 - progress // 5)
            print(f"\r  [{bar}] {progress}% ({offset}/{fw_size})", end="", flush=True)

        print()  # 换行

        # 3. CMD_VERIFY：发送 CRC32 校验
        print("\n[3/4] 发送 CMD_VERIFY...")
        verify_data = bytes([CMD_VERIFY]) + struct.pack("<I", fw_crc32)
        send_frame(ser, seq & 0xFF, verify_data)
        if not wait_ack(ser, "CRC32 校验失败"):
            return False

        # 4. CMD_GO：跳转到应用程序
        print("\n[4/4] 发送 CMD_GO...")
        go_data = bytes([CMD_GO])
        send_frame(ser, (seq + 1) & 0xFF, go_data)
        if not wait_ack(ser, "CMD_GO 失败"):
            return False

        print("\n✅ 烧录完成！应用程序已启动。")
        return True

    finally:
        ser.close()


def main():
    if len(sys.argv) < 3:
        print(f"用法：{sys.argv[0]} <串口> <固件.bin> [波特率]")
        print(f"示例：{sys.argv[0]} COM3 app.bin 115200")
        sys.exit(1)

    port = sys.argv[1]
    firmware_path = sys.argv[2]
    baudrate = int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_BAUD

    success = flash(port, firmware_path, baudrate)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
