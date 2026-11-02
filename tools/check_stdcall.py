"""Check that each shim pops as many argument bytes as the real Windows API.

Windows functions are stdcall: the callee removes its arguments with `ret N`.
A shim with the wrong parameter list corrupts the game's stack. mingw's import
libraries record the true size in decorated names (_Pie@36), and objdump shows
the `ret` instructions of our compiled functions.

Usage: check_stdcall.py <chiron binary> <mingw lib dir> <generated export .c>...
"""
import re
import subprocess
import sys

binary, libdir, *tables = sys.argv[1:]

dlls = {'kernel32', 'user32', 'gdi32', 'advapi32', 'winmm', 'dsound', 'ole32', 'ddraw', 'dplayx', 'msvfw32'}
expected = {}
for dll in dlls:
    try:
        out = subprocess.run(['i686-w64-mingw32-nm', f'{libdir}/lib{dll}.a'], capture_output=True, text=True).stdout
    except FileNotFoundError:
        sys.exit('i686-w64-mingw32-nm not found (run inside nix develop)')
    for m in re.finditer(r'__imp__(\w+)@(\d+)', out):
        expected.setdefault(m.group(1), int(m.group(2)))

exports = []
for t in tables:
    exports += re.findall(r'\{"(\w+)", \d+, \(void \*\)(\w+)\}', open(t).read())

# Collect the ret sizes of every function in the binary.
dis = subprocess.run(['objdump', '-d', '--no-show-raw-insn', binary], capture_output=True, text=True).stdout
rets, current = {}, None
for line in dis.splitlines():
    if not line.strip():
        current = None  # objdump ends each function with a blank line
        continue
    m = re.match(r'^[0-9a-f]+ <(\w+)>:', line)
    if m:
        current = m.group(1)
        continue
    m = re.search(r'\bret\s*(\$0x([0-9a-f]+))?', line)
    if current and m and re.search(r'\tret', line):
        rets.setdefault(current, set()).add(int(m.group(2), 16) if m.group(2) else 0)

bad = 0
for name, fn in exports:
    if name not in expected:
        continue  # cdecl (wsprintfA) or not in mingw's libraries
    got = rets.get(fn)
    if got is None:
        print(f'?    {name}: no ret found')
        continue
    if got != {expected[name]}:
        print(f'BAD  {name}: pops {sorted(got)} bytes, Windows pops {expected[name]}')
        bad += 1
print(f'{len(exports)} exports checked, {bad} mismatched')
sys.exit(1 if bad else 0)
