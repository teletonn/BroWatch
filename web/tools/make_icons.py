#!/usr/bin/env python3
"""Генерация иконок BroWatch Web из кадра 0 LILGUY (1:1 с прошивкой).
Только stdlib: icon.svg + icon-192.png + icon-512.png в static/.
Перегенерировать после правок: python3 tools/make_icons.py"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
STATIC = os.path.join(os.path.dirname(HERE), "static")

# Кадр 0 LILGUY 10x10, 2 бита на пиксель, пиксель xx — биты (xx*2)..(xx*2+1).
FRAME0 = [0x000000, 0x001540, 0x000950, 0x002A40, 0x000E40,
          0x000E40, 0x000E00, 0x000F00, 0x000F80, 0x000A00]
PAL = {1: (0x00, 0xFF, 0xF5), 2: (0xFF, 0xD0, 0xF0), 3: (0xB9, 0x67, 0xFF)}
BG = (0x0A, 0x00, 0x0F)


def pixels():
    out = []
    for yy, row in enumerate(FRAME0):
        for xx in range(10):
            c = (row >> (xx * 2)) & 3
            if c:
                out.append((xx, yy, PAL[c]))
    return out


def write_svg(px, path):
    rects = "".join(
        '<rect x="%d" y="%d" width="1" height="1" fill="#%02x%02x%02x"/>'
        % (x, y, r, g, b) for x, y, (r, g, b) in px)
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-1 -1 12 12">'
           '<rect x="-1" y="-1" width="12" height="12" fill="#0a000f"/>'
           + rects + '</svg>')
    with open(path, "w") as f:
        f.write(svg)


def write_png(px, size, path):
    cell = size // 12
    pad = (size - cell * 10) // 2
    raw = b""
    for yy in range(size):
        raw += b"\x00"
        for xx in range(size):
            gx, gy = (xx - pad) // cell, (yy - pad) // cell
            col = BG
            if 0 <= gx < 10 and 0 <= gy < 10 and (xx - pad) % cell >= 0 and (yy - pad) % cell >= 0:
                for x, y, c in px:
                    if x == gx and y == gy:
                        col = c
                        break
            raw += bytes(col)
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    px = pixels()
    print("lil guy pixels: %d" % len(px))
    write_svg(px, os.path.join(STATIC, "icon.svg"))
    write_png(px, 192, os.path.join(STATIC, "icon-192.png"))
    write_png(px, 512, os.path.join(STATIC, "icon-512.png"))
    print("icons written")


if __name__ == "__main__":
    main()
