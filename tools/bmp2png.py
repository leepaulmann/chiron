"""Convert an uncompressed 24/32-bit BMP (as written by CHIRON_SNAPSHOT) to PNG."""
import struct
import sys
import zlib

src, dst = sys.argv[1:3]
d = open(src, 'rb').read()
off = struct.unpack_from('<I', d, 10)[0]
w, h, _, bpp = struct.unpack_from('<iiHH', d, 18)
step = bpp // 8
stride = (w * step + 3) & ~3
rows = []
for y in range(abs(h)):
    sy = abs(h) - 1 - y if h > 0 else y
    row = d[off + sy * stride: off + sy * stride + w * step]
    rgb = bytearray()
    for x in range(w):
        b, g, r = row[x * step: x * step + 3]
        rgb += bytes((r, g, b))
    rows.append(b'\0' + bytes(rgb))
def chunk(t, data):
    return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, abs(h), 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
open(dst, 'wb').write(png)
