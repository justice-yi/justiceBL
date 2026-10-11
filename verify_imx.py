#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
verify_imx.py —— 镜像自检（黄金对拍）

规则：生成二进制格式的代码，上硬件前必须先和已知 good 的参考逐字节 diff。
本脚本用独立于 mkimx.py 的解析器（小端）断言：
  1. DDR 变体的 IVT/BootData 除 length 外每个字段 == 官方镜像（字节序陷阱的哨兵）
  2. DDR 变体的 DCD 488 字节 == 官方镜像（拷贝完整性）
  3. self == start + 0x400、entry == 链接基址、OCRAM 版 dcd==0（布局自洽）
  4. 0xC00 处首字是 b 指令（代码落位）

用法：verify_imx.py <ours-ddr.imx> <official.imx> <ours-ocram.imx>
"""

import struct
import sys


def fields(path):
	d = open(path, "rb").read()
	g = lambda o: struct.unpack("<I", d[o:o + 4])[0]
	return d, {
		"header": d[0:4].hex(), "entry": g(4), "rsvd1": g(8), "dcd": g(0xC),
		"bd_ptr": g(0x10), "self": g(0x14), "csf": g(0x18), "rsvd2": g(0x1C),
		"start": g(0x20), "length": g(0x24), "plugin": g(0x28),
	}


def main():
	ddr_p, off_p, ocram_p = sys.argv[1:4]
	ddr, ddrf = fields(ddr_p)
	off, offf = fields(off_p)
	ocram, ocramf = fields(ocram_p)
	ok = True

	for k in ddrf:			# 1. 与官方逐字段对拍（length 允许不同）
		if k == "length":
			continue
		if ddrf[k] != offf[k]:
			print(f"FAIL DDR.{k}: 0x{ddrf[k]:08X} != 官方 0x{offf[k]:08X}")
			ok = False

	if ddr[0x2C:0x214] != off[0x2C:0x214]:		# 2. DCD 对拍
		print("FAIL: DCD 488 字节与官方不一致")
		ok = False

	for name, f in [("ddr", ddrf), ("ocram", ocramf)]:	# 3. 布局自洽
		if f["self"] != f["start"] + 0x400 or f["plugin"] != 0:
			print(f"FAIL {name}: self/start/plugin 关系错")
			ok = False
	if ocramf["dcd"] != 0 or ocramf["entry"] != 0x00908000:
		print("FAIL ocram: dcd 应为 NULL / entry 应为 0x00908000")
		ok = False

	for d, name in [(ddr, "ddr"), (ocram, "ocram")]:	# 4. 代码落位
		if struct.unpack("<I", d[0xC00:0xC04])[0] >> 24 != 0xEA:
			print(f"FAIL {name}: 0xC00 首字不是 b 指令")
			ok = False

	print("PASS: 字段对拍 + DCD 对拍 + 布局自洽 + 代码落位" if ok else "存在差异，见上")
	sys.exit(0 if ok else 1)


if __name__ == "__main__":
	main()
