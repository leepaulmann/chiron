"""How much of terranx.exe's code Chiron has replaced.

Reads your own copy of terranx.exe (the path given, else the game folder
from ~/.config/chiron/chiron.conf, else ~/.local/share/chiron/game) and
the HOOK tables in src/engine/*.c, and prints, for each 64 KB block of
the code, how many bytes the hooked functions cover. A function runs from
its address to the next function start; starts are found as the usual
`push ebp; mov ebp, esp` after padding, so the totals are estimates.

    python3 tools/coverage.py [terranx.exe]

The game logic lies roughly in 0x4e0000-0x5cffff; below it are the
screens, above it the game's window, sprite, font and network library.
"""
import bisect
import glob
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGIC = (0x4E0000, 0x5D0000)
LIBRARY = 0x640000  # the C runtime and other linked libraries start about here


def find_exe():
    if len(sys.argv) > 1:
        return sys.argv[1]
    game = os.path.expanduser('~/.local/share/chiron/game')
    conf = os.path.expanduser('~/.config/chiron/chiron.conf')
    if os.path.exists(conf):
        for line in open(conf, encoding='utf-8', errors='replace'):
            key, _, value = line.strip().partition('=')
            if key.strip() == 'game' and value.strip():
                game = value.strip()
    for name in os.listdir(game):
        if name.lower() == 'terranx.exe':
            return os.path.join(game, name)
    sys.exit('terranx.exe not found; give its path')


def text_section(path):
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    for i in range(count):
        name, _, va, size, raw = struct.unpack_from('<8sIIII', data, pe + 24 + opt + 40 * i)
        if name.rstrip(b'\0') == b'.text':
            return base + va, data[raw:raw + size]
    sys.exit('no .text section')


def hooked():
    found = set()
    for path in glob.glob(os.path.join(ROOT, 'src', 'engine', '*.c')):
        for m in re.finditer(r'^\s*HOOK\((0x[0-9a-fA-F]+)', open(path).read(), re.M):
            found.add(int(m.group(1), 16))
    return found


def main():
    start, text = text_section(find_exe())
    end = start + len(text)
    starts = {start}
    for off in range(16, len(text) - 3, 16):
        if text[off - 1] in (0x90, 0xCC, 0xC3, 0xC2) and text[off:off + 3] == b'\x55\x8b\xec':
            starts.add(start + off)
    hooks = hooked()
    bounds = sorted(starts | hooks | {LIBRARY})

    def size(a):
        i = bisect.bisect_right(bounds, a)
        return (bounds[i] if i < len(bounds) else end) - a

    blocks = {}
    for a in bounds:
        if a >= LIBRARY:
            continue
        b = blocks.setdefault(a >> 16, [0, 0, 0, 0])
        s = size(a)
        b[0] += 1
        b[1] += s
        if a in hooks:
            b[2] += 1
            b[3] += s
    print('block      functions   KB   hooked   KB  share')
    totals = {'logic': [0, 0, 0, 0], 'all': [0, 0, 0, 0]}
    for k in sorted(blocks):
        n, s, hn, hs = blocks[k]
        mark = '*' if LOGIC[0] <= k << 16 < LOGIC[1] else ' '
        print('%06x %s %6d %6d %6d %6d %5d%%' % (k << 16, mark, n, s // 1024, hn, hs // 1024, 100 * hs // max(s, 1)))
        for key in ('all', 'logic') if mark == '*' else ('all',):
            t = totals[key]
            for i, v in enumerate((n, s, hn, hs)):
                t[i] += v
    for key, label in (('logic', 'game logic (*)'), ('all', 'all game code')):
        n, s, hn, hs = totals[key]
        print('%-16s %d of %d functions, %d of %d KB (%d%%)' % (label, hn, n, hs // 1024, s // 1024, 100 * hs // s))


if __name__ == '__main__':
    main()
