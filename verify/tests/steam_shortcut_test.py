#!/usr/bin/env python3
"""Tests packaging/steamdeck/steam_shortcut.py on temporary Steam profiles
(see docs/STEAM_DECK.md, release checks). Never touches a real Steam."""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "packaging", "steamdeck", "steam_shortcut.py")
sys.path.insert(0, os.path.dirname(TOOL))
import steam_shortcut as ss  # noqa: E402

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


def run(home, *args, root=None):
    env = dict(os.environ, HOME=home)
    cmd = [sys.executable, "-I", TOOL, "--exe", EXE, "--start-dir", os.path.dirname(EXE), "--launch-options=--fullscreen",
           "--artwork", ART] + list(args)
    if root:
        cmd += ["--steam-root", root]
    return subprocess.run(cmd, env=env, capture_output=True, text=True)


def entries(vdf):
    return ss.parse(open(vdf, "rb").read())[0][2]


def ours(vdf):
    return [e for _, _, e in entries(vdf) if ss.field(e, "Exe")[1] == ('"%s"' % EXE).encode()]


tmp = tempfile.mkdtemp(prefix="steam-test-")
EXE = os.path.join(tmp, "Games dir", "Super_Mario_Land_Port-x86_64.AppImage")
ART = os.path.join(tmp, "art")
os.makedirs(ART)
for name in ss.ARTWORK:
    open(os.path.join(ART, name), "wb").write(b"art-" + name.encode())

other = [(ss.T_INT, b"appid", struct.pack("<I", 0x81234567)), (ss.T_STR, b"AppName", b"Other"),
         (ss.T_STR, b"Exe", b'"/usr/bin/other"'), (ss.T_STR, b"LaunchOptions", b"-x"),
         (ss.T_MAP, b"tags", [(ss.T_STR, b"0", b"fav")])]
original = ss.serialise([(ss.T_MAP, b"shortcuts", [(ss.T_MAP, b"0", other)])])

# standard layout, one account, an unrelated shortcut and custom artwork
home = os.path.join(tmp, "home1")
cfg = os.path.join(home, ".local", "share", "Steam", "userdata", "1234", "config")
os.makedirs(os.path.join(cfg, "grid"))
vdf = os.path.join(cfg, "shortcuts.vdf")
open(vdf, "wb").write(original)
r = run(home)
check(r.returncode == 0, "install: " + r.stdout + r.stderr)
mine = ours(vdf)
check(len(mine) == 1, "one shortcut")
appid = struct.unpack("<I", ss.field(mine[0], "appid")[1])[0]
check(ss.field(mine[0], "AppName")[1] == b"Super Mario Land", "name")
check(ss.field(mine[0], "LaunchOptions")[1] == b"--fullscreen", "launch options")
check(entries(vdf)[0] == (ss.T_MAP, b"0", other), "unrelated shortcut preserved")
check(open(vdf + ".super-mario-land.bak", "rb").read() == original, "backup of the original")
grid = os.path.join(cfg, "grid")
check(open(os.path.join(grid, "%dp.png" % appid), "rb").read() == b"art-portrait.png", "artwork")
open(os.path.join(grid, "%d_hero.png" % appid), "wb").write(b"mine")

# repeated install: same ID, no duplicate, custom artwork kept; name migration keeps the ID
r = run(home, "--name", "Super Mario Land (C port)")
r = run(home)
check(r.returncode == 0 and len(ours(vdf)) == 1, "repeat install, no duplicate")
check(struct.unpack("<I", ss.field(ours(vdf)[0], "appid")[1])[0] == appid, "stable app ID")
check(open(os.path.join(grid, "%d_hero.png" % appid), "rb").read() == b"mine", "custom artwork kept")
r = run(home, "--repair-artwork")
check(open(os.path.join(grid, "%d_hero.png" % appid), "rb").read() == b"art-hero.png", "artwork repair")

# malformed and truncated files stay untouched
for bad in (original[:-3], original + b"x", b"\x00shortcuts\x00\x09"):
    open(vdf, "wb").write(bad)
    r = run(home)
    check(r.returncode == 4 and open(vdf, "rb").read() == bad, "malformed untouched %r" % bad[-4:])

# two existing matches are refused
dup = ss.serialise([(ss.T_MAP, b"shortcuts", [
    (ss.T_MAP, b"0", [(ss.T_STR, b"Exe", ('"%s"' % EXE).encode())]),
    (ss.T_MAP, b"1", [(ss.T_STR, b"exe", ('"%s"' % EXE).encode())])])])
open(vdf, "wb").write(dup)
check(run(home).returncode == 4 and open(vdf, "rb").read() == dup, "duplicates refused")

# Flatpak layout, no shortcuts.vdf yet, several accounts
home = os.path.join(tmp, "home2")
flat = os.path.join(home, ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam")
for user in ("111", "222"):
    os.makedirs(os.path.join(flat, "userdata", user, "config"))
r = run(home)
check(r.returncode == 2 and "--user" in r.stdout, "several accounts need a choice")
os.makedirs(os.path.join(flat, "config"))
open(os.path.join(flat, "config", "loginusers.vdf"), "w").write(
    '"users"\n{\n\t"76561197960265950"\n\t{\n\t\t"AccountName"\t\t"x"\n\t\t"MostRecent"\t\t"1"\n\t}\n}\n')
r = run(home)   # 76561197960265950 & 0xFFFFFFFF = 222
check(r.returncode == 0 and os.path.exists(os.path.join(flat, "userdata", "222", "config", "shortcuts.vdf")),
      "Flatpak, most recent account: " + r.stdout)
check(not os.path.exists(os.path.join(flat, "userdata", "111", "config", "shortcuts.vdf")), "other account untouched")
r = run(home, "--user", "111")
check(r.returncode == 0 and os.path.exists(os.path.join(flat, "userdata", "111", "config", "shortcuts.vdf")), "--user")

# Steam running: nothing is written (a copy of sleep named "steam")
fake = os.path.join(tmp, "steam")
import shutil  # noqa: E402
shutil.copy(shutil.which("sleep"), fake)
try:
    proc = subprocess.Popen([fake, "30"])
except OSError:
    proc = None
    print("note: cannot run a fake steam process here (noexec); skipped")
if proc:
    vdf2 = os.path.join(flat, "userdata", "222", "config", "shortcuts.vdf")
    before = open(vdf2, "rb").read()
    r = run(home, "--name", "Renamed")
    proc.kill()
    proc.wait()
    check(r.returncode == 3 and open(vdf2, "rb").read() == before, "Steam running: untouched")

# no Steam at all
check(run(os.path.join(tmp, "nohome")).returncode == 2, "no Steam")

print("steam_shortcut_test: %s" % ("all passed" if not failures else "%d failure(s)" % failures))
sys.exit(1 if failures else 0)
