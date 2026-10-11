#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
mkimx.py —— 把 justiceBL.bin 包成 ROM 可引导镜像

ROM 搬运规则（正点原子《I.MX6U 开发指南》9.4 节 + 官方 u-boot-dtb.imx 头实测）：
  设备偏移 D 的字节 → RAM 地址 start + D
  即 boot_data.start 对应"卡的第 0 字节"，比 IVT 落点(=self)低 0x400！

镜像布局（文件 dd 到设备 1KB 偏移处；RAM 地址 = start + 设备偏移）：

  文件偏移  设备偏移  RAM 地址         内容
  0x000     0x400    self(=start+0x400)  IVT 32B
  0x020     0x420    self+0x20         Boot Data 12B（注意只有 3 个字）
  0x02C     0x42C    self+0x2C         DCD（可选；无则 dcd 指针 NULL）
  0xC00     0x1000   start+0x1000      代码（= 链接地址，start = 链接地址-0x1000）

用法：mkimx.py <in.bin> <out.imx> [base_hex] [dcd.bin]
  默认 OCRAM：base=0x00908000，无 DCD
  DDR 变体：  mkimx.py x.bin x.imx 0x87800000 dcd.bin
烧写：dd if=<out.imx> of=/dev/sdX bs=512 seek=2 conv=notrunc
"""

import struct
import sys

HDR = 0xC00				# 代码在文件内的偏移（官方/正点原子同款布局）


def le32(v):
	return struct.pack("<I", v)	# 小端！官方镜像实测；RM 的 big-endian 仅指 header 的 tag/length


def main():
	bin_path, out_path = sys.argv[1], sys.argv[2]
	base = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x00908000
	dcd = b""
	if len(sys.argv) > 4:
		with open(sys.argv[4], "rb") as f:
			dcd = f.read()		# 含 D2 头的完整 DCD 表

	start = base - 0x1000			# 设备 0 字节对应的 RAM 地址
	self_ = start + 0x400			# IVT 落点
	code = open(bin_path, "rb").read()
	code += b"\0" * (-len(code) % 4)
	length = 0x1000 + len(code)

	ivt = bytes([0xD1, 0x00, 0x20, 0x40])	# header
	ivt += le32(base)			# entry = 链接地址 = _start
	ivt += le32(0)				# reserved1
	ivt += le32(self_ + 0x2C if dcd else 0)	# dcd 指针（或 NULL）
	ivt += le32(self_ + 0x20)		# boot data 指针
	ivt += le32(self_)			# self
	ivt += le32(0)				# csf = NULL
	ivt += le32(0)				# reserved2
	assert len(ivt) == 0x20

	bd = le32(start) + le32(length) + le32(0)	# start / length / plugin

	assert len(ivt) + len(bd) + len(dcd) <= HDR, "DCD 太长"

	with open(out_path, "wb") as f:
		f.write(ivt)			# 0x00
		f.write(bd)			# 0x20（12 字节）
		f.write(dcd)			# 0x2C 起（若有）
		f.write(b"\0" * (HDR - 0x20 - 12 - len(dcd)))
		f.write(code)			# 0xC00

	print(f"{out_path}: {HDR + len(code)} bytes, base 0x{base:08X}, "
	      f"start 0x{start:08X}, self 0x{self_:08X}, "
	      f"dcd={'yes' if dcd else 'NULL'}")


if __name__ == "__main__":
	main()
