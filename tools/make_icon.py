#!/usr/bin/env python3
"""Generates icon.png (48x48) without any dependencies."""
import struct, zlib, math
N = 48
px = []
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
for y in range(N):
    row = []
    for x in range(N):
        # rounded square mask
        r = 11
        cx = min(max(x, r), N - 1 - r); cy = min(max(y, r), N - 1 - r)
        d = math.hypot(x - cx, y - cy)
        a = 255 if d <= r else (int(255 * max(0, r + 1 - d)) if d < r + 1 else 0)
        c = mix((0x7C, 0x9C, 0xFF), (0x3A, 0x2A, 0x8F), (x + y) / (2 * N))
        # play triangle
        tx, ty = x - 19, y - 24
        if 0 <= tx <= 18 and abs(ty) <= (18 - tx) * 0.9 * (1 if tx > 0 else 1) * 0.95 and tx <= 17:
            if abs(ty) <= 11 - tx * 0.6:
                c = (0x0B, 0x0D, 0x12)
        row.append((*c, a))
    px.append(row)
raw = b''.join(b'\x00' + b''.join(bytes(p) for p in row) for row in px)
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', N, N, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
open('icon.png', 'wb').write(png)
