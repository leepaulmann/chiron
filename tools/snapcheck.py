#!/usr/bin/env python3
"""Compare the screens Chiron draws with the original's under a scripted scene.

    tools/snapcheck.py SCENE [--out DIR] [--modes original,on] [--bin PATH]

SCENE is a file holding a CHIRON_SCRIPT (steps may be split over lines;
'#' starts a comment). Its "snap NAME" steps save the frame on screen; the
scene should end with a "close" step. The scene runs once per mode (the
game's own code with --original, then Chiron's) with the calendar clock held
(CHIRON_FIXED_TIME), so Quick Start builds the same map both times, each in a
fresh user folder under DIR. Then each snap is compared pixel by pixel: the
differing area's bounding box and pixel count are printed, and a diff image
(differing pixels in red over a dimmed frame) is written to DIR.

Blinking cursors and animations change from frame to frame on their own
timers, so each snap is a burst of frames 0.2 s apart over 2 s. A pixel
that changes within either run's burst is left out of the comparison (and
counted as animated); every other pixel must match exactly. Leave at least 2.5
seconds after a snap before the next step.

A scene whose first step is "<seconds> load" starts from a saved game instead
of a new one: the tool first runs Quick Start once with the original's code to
get the game's turn-one autosave, copies it into each run's user folder as
saves/scene.SAV, and the step opens it from the title screen (taking 7 s;
start it at 25 s, once the title screen is up). The rest of the
game's randomness while a map is built depends on timing, so a scene that
builds its own map can come out slightly different between runs; a loaded
game doesn't.

Exit status: 0 when every snap matches, 1 when any differ or are missing.
Needs the game set up (see README). Nothing it writes belongs in the repo.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXED_TIME = "946728000"  # 2000-01-01 12:00 UTC


def read_scene(path):
    steps = []
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                steps.extend(s.strip() for s in line.split(";") if s.strip())
    return steps


def read_bmp(path):
    """A 24- or 32-bit BMP as (width, height, top-down rows of BGR bytes)."""
    with open(path, "rb") as f:
        data = f.read()
    off, = struct.unpack_from("<I", data, 10)
    w, h, _, bpp = struct.unpack_from("<iiHH", data, 18)
    if bpp not in (24, 32):
        raise ValueError(f"{path}: {bpp} bpp, expected 24 or 32")
    px = bpp // 8
    stride = (w * px + 3) & ~3
    rows = []
    for y in range(abs(h)):
        r = data[off + y * stride: off + y * stride + w * px]
        rows.append(r if px == 3 else b"".join(r[i:i + 3] for i in range(0, len(r), 4)))
    if h > 0:
        rows.reverse()
    return w, abs(h), rows


def write_bmp(path, w, h, rows):
    stride = (w * 3 + 3) & ~3
    pad = b"\0" * (stride - w * 3)
    size = 54 + stride * h
    hdr = struct.pack("<2sIHHI", b"BM", size, 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, stride * h, 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + info + b"".join(r + pad for r in rows))


BURST = [k / 5 for k in range(11)]  # 0 to 2 s


def compare(a_paths, b_paths, diff_path):
    frames = []
    for p in a_paths + b_paths:
        w, h, rows = read_bmp(p)
        frames.append((w, h, rows))
    sizes = {(w, h) for w, h, _ in frames}
    if len(sizes) != 1:
        return f"frame sizes differ: {sorted(sizes)}"
    w, h = sizes.pop()
    a = [f[2] for f in frames[:len(a_paths)]]
    b = [f[2] for f in frames[len(a_paths):]]
    count, animated, box = 0, 0, None
    out = []
    for y in range(h):
        ra, rb = a[0][y], b[0][y]
        steady = all(r[y] == ra for r in a) and all(r[y] == rb for r in b)
        if steady and ra == rb:
            out.append(bytes(v >> 2 for v in ra))
            continue
        row = bytearray(v >> 2 for v in ra)
        for x in range(w):
            i = x * 3
            pa = ra[i:i + 3]
            pb = rb[i:i + 3]
            if not steady and (any(r[y][i:i + 3] != pa for r in a) or any(r[y][i:i + 3] != pb for r in b)):
                animated += 1
                continue
            if pa != pb:
                count += 1
                row[i:i + 3] = b"\x00\x00\xff"
                box = (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)) if box else (x, y, x, y)
        out.append(bytes(row))
    note = f" ({animated} animated pixels skipped)" if animated else ""
    if not count:
        return None, note
    write_bmp(diff_path, w, h, out)
    return f"{count} pixels differ in ({box[0]},{box[1]})-({box[2]},{box[3]}), see {diff_path}", note


LOAD_GAME = (835, 512)       # title screen: Load Game
SCENE_SAVE = (417, 269)      # Load dialog: the second file under saves/
AUTOSAVE = os.path.join("saves", "auto", "Alpha Centauri Autosave 1.SAV")


def make_save(out, binary):
    """Quick Start once with the original's code; return its first autosave."""
    run("prepare", ["30 click 835 362", "40 key Return", "50 key b", "58 key Return", "68 close"], out, binary, 100)
    path = os.path.join(out, "user-prepare", AUTOSAVE)
    if not os.path.exists(path):
        sys.exit(f"no autosave at {path}; see {out}/prepare.log")
    return path


def run(mode, steps, out, binary, timeout, save=None):
    user = os.path.join(out, f"user-{mode}")
    shutil.rmtree(user, ignore_errors=True)
    os.makedirs(user)
    if save:
        os.makedirs(os.path.join(user, "saves"))
        shutil.copy(save, os.path.join(user, "saves", "scene.SAV"))
    for f in os.listdir(out):
        if f.startswith(mode + "-") and f.endswith(".bmp"):
            os.remove(os.path.join(out, f))
    script = []
    for s in steps:
        at, _, action = s.partition(" ")
        if action == "load":
            script.append((float(at), "click %d %d" % LOAD_GAME))
            script.append((float(at) + 3, "dblclick %d %d" % SCENE_SAVE))
            script.append((float(at) + 7, "key Return"))  # the faction and difficulty note
            continue
        if action.startswith("snap "):
            name = action[5:].strip()
            for k, dt in enumerate(BURST):
                script.append((float(at) + dt, "snap " + os.path.join(out, f"{mode}-{name}.{k}.bmp")))
        else:
            script.append((float(at), action))
    script.sort(key=lambda t: t[0])
    script = [f"{at:g} {action}" for at, action in script]
    env = dict(os.environ, CHIRON_USER_DIR=user, CHIRON_FIXED_TIME=FIXED_TIME, CHIRON_MUTE="1",
               CHIRON_SCRIPT="; ".join(script))
    env.pop("CHIRON_REIMPL", None)
    env.pop("CHIRON_SNAPSHOT", None)
    args = [binary] + (["--original"] if mode.startswith(("original", "prepare")) else [])
    with open(os.path.join(out, f"{mode}.log"), "w") as log:
        try:
            r = subprocess.run(["timeout", "-s", "INT", str(timeout)] + args, cwd=ROOT, env=env,
                               stdout=log, stderr=subprocess.STDOUT)
        except KeyboardInterrupt:
            sys.exit(130)
    return r.returncode


def attempt(a, steps, snaps, modes, save):
    """Run every mode and compare the snaps; returns {snap: problem or None}."""
    for m in modes:
        status = run(m, steps, a.out, a.bin, a.timeout, save)
        print(f"{m}: exit status {status}")
    results = {}
    for name in snaps:
        paths = {m: [os.path.join(a.out, f"{m}-{name}.{k}.bmp") for k in range(len(BURST))] for m in modes}
        missing = [x for ps in paths.values() for x in ps if not os.path.exists(x)]
        if missing:
            results[name] = f"{len(missing)} frames missing, e.g. {missing[0]}"
            print(f"{name}: {results[name]}")
            continue
        results[name] = None
        for m in modes[1:]:
            r, note = compare(paths[modes[0]], paths[m], os.path.join(a.out, f"diff-{name}.bmp"))
            print(f"{name}: " + (r or "match") + note)
            results[name] = results[name] or r
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("scene")
    p.add_argument("--out", default=os.environ.get("TMPDIR", "/tmp") + "/snapcheck")
    p.add_argument("--modes", default="original,on")
    p.add_argument("--bin", default=os.path.join(ROOT, "build", "chiron"))
    p.add_argument("--timeout", type=int, default=300)
    p.add_argument("--retries", type=int, default=1, help="rerun once when snaps differ (0 to report the first attempt)")
    a = p.parse_args()
    steps = read_scene(a.scene)
    snaps = [s.split("snap", 1)[1].strip() for s in steps if s.partition(" ")[2].startswith("snap ")]
    os.makedirs(a.out, exist_ok=True)
    modes = a.modes.split(",")
    save = make_save(a.out, a.bin) if steps and steps[0].split()[1:] == ["load"] else None
    results = attempt(a, steps, snaps, modes, save)
    if any(r for r in results.values()) and a.retries:
        # A scene runs on wall-clock time, so a slow load now and then puts a
        # click on the wrong thing. A real difference shows up again.
        print("differences found; running the scene again")
        first = results
        results = attempt(a, steps, snaps, modes, save)
        for name, r in results.items():
            if first[name] and not r:
                print(f"{name}: differed only on the first attempt (timing), matches now")
    bad = sum(1 for r in results.values() if r)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
