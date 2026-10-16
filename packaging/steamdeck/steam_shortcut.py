#!/usr/bin/env python3
"""Adds or repairs the "Super Mario Land" non-Steam game shortcut and its
library artwork. Written for this port; follows the safeguards listed in
docs/STEAM_DECK.md:

- the binary shortcuts.vdf is parsed completely and must re-serialise to the
  identical bytes before anything is changed; otherwise it is left untouched
- the shortcut is matched by its Exe field only (one match is repaired, more
  than one is refused); its app ID and unrelated fields and shortcuts are kept
- the original file is backed up, the new one written with fsync and an
  atomic rename; nothing is written while Steam is running (Steam rewrites
  shortcuts.vdf on exit), and that is checked again right before the rename
- standard and Flatpak Steam roots; with several accounts the most recent
  login is used unless --user picks one
- artwork is copied to the account's grid directory without replacing the
  user's own files unless --repair-artwork is given

    steam_shortcut.py --exe PATH --start-dir DIR --artwork DIR
                      [--name NAME] [--launch-options OPTS] [--icon PNG]
                      [--user ID] [--steam-root DIR] [--repair-artwork]
                      [--artwork-only] [--list-users]
Exit codes: 0 done, 2 usage/no Steam, 3 Steam is running, 4 refused (malformed
or ambiguous shortcuts.vdf).
"""
import argparse
import glob
import os
import re
import shutil
import struct
import sys
import tempfile
import time
import zlib

T_MAP, T_STR, T_INT, T_FLOAT, T_PTR, T_WSTR, T_COLOR, T_U64, T_END, T_I64 = 0, 1, 2, 3, 4, 5, 6, 7, 8, 10
FIXED = {T_INT: 4, T_FLOAT: 4, T_PTR: 4, T_COLOR: 4, T_U64: 8, T_I64: 8}


class Malformed(Exception):
    pass


def parse_map(data, pos):
    """Returns ([(type, key, value)], pos after the end marker)."""
    items = []
    while True:
        if pos >= len(data):
            raise Malformed("truncated")
        kind = data[pos]
        pos += 1
        if kind == T_END:
            return items, pos
        end = data.find(b"\0", pos)
        if end < 0:
            raise Malformed("unterminated key")
        key = data[pos:end]
        pos = end + 1
        if kind == T_MAP:
            value, pos = parse_map(data, pos)
        elif kind == T_STR:
            end = data.find(b"\0", pos)
            if end < 0:
                raise Malformed("unterminated string")
            value, pos = data[pos:end], end + 1
        elif kind in FIXED:
            n = FIXED[kind]
            if pos + n > len(data):
                raise Malformed("truncated value")
            value, pos = data[pos:pos + n], pos + n
        else:
            raise Malformed("unknown field type %d" % kind)
        items.append((kind, key, value))


def parse(data):
    items, pos = parse_map(data, 0)
    if pos != len(data):
        raise Malformed("trailing data")
    if serialise(items) != data:
        raise Malformed("does not round-trip")
    return items


def serialise(items):
    out = bytearray()
    for kind, key, value in items:
        out.append(kind)
        out += key + b"\0"
        if kind == T_MAP:
            out += serialise(value)
        elif kind == T_STR:
            out += value + b"\0"
        else:
            out += value
    out.append(T_END)
    return bytes(out)


def field(entry, name):
    if isinstance(name, str):
        name = name.encode()
    for kind, key, value in entry:
        if key.lower() == name.lower():
            return kind, value
    return None, None


def set_field(entry, kind, name, value):
    for i, (k, key, _) in enumerate(entry):
        if key.lower() == name.lower():
            entry[i] = (kind, key, value)
            return
    entry.append((kind, name, value))


def shortcut_appid(exe, name):
    """Steam's ID for a new non-Steam shortcut (grid files use it unsigned)."""
    return (zlib.crc32((exe + name).encode()) & 0xFFFFFFFF) | 0x80000000


def steam_running():
    for comm in glob.glob("/proc/[0-9]*/comm"):
        try:
            if open(comm).read().strip() in ("steam", "steamwebhelper"):
                return True
        except OSError:
            pass
    return False


def steam_roots(extra):
    home = os.path.expanduser("~")
    cands = ([extra] if extra else []) + [
        os.path.join(home, ".steam", "steam"),
        os.path.join(home, ".local", "share", "Steam"),
        os.path.join(home, ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam"),
        os.path.join(home, ".var", "app", "com.valvesoftware.Steam", ".steam", "steam"),
    ]
    roots = []
    for c in cands:
        if c and os.path.isdir(os.path.join(c, "userdata")):
            real = os.path.realpath(c)
            if real not in roots:
                roots.append(real)
    return roots


def users(root):
    ids = [d for d in os.listdir(os.path.join(root, "userdata")) if d.isdigit() and d != "0"]
    return sorted(ids)


def most_recent_user(root, ids):
    """The account with MostRecent "1" in config/loginusers.vdf (64-bit IDs)."""
    try:
        text = open(os.path.join(root, "config", "loginusers.vdf"), encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    for steam64, body in re.findall(r'"(\d{17})"\s*\{([^}]*)\}', text):
        if re.search(r'"(?:MostRecent|mostrecent)"\s*"1"', body):
            account = str(int(steam64) & 0xFFFFFFFF)
            if account in ids:
                return account
    return None


def write_atomic(path, data):
    d = os.path.dirname(path)
    fd, tmp = tempfile.mkstemp(prefix=".shortcuts.", dir=d)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        if steam_running():
            raise RuntimeError("steam started")
        os.replace(tmp, path)
        dfd = os.open(d, os.O_RDONLY)
        try:
            os.fsync(dfd)
        finally:
            os.close(dfd)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def update_shortcuts(vdf_path, exe, start_dir, name, launch, icon):
    """Returns the shortcut's app ID (unsigned)."""
    if os.path.exists(vdf_path):
        data = open(vdf_path, "rb").read()
        root = parse(data)
    else:
        data = None
        root = [(T_MAP, b"shortcuts", [])]
    if len(root) != 1 or root[0][0] != T_MAP or root[0][1].lower() != b"shortcuts":
        raise Malformed("no shortcuts map")
    shortcuts = root[0][2]
    quoted = '"%s"' % exe
    matches = [e for e in shortcuts if e[0] == T_MAP and
               (field(e[2], "Exe")[1] or b"").decode("utf-8", "replace").strip('"') == exe]
    if len(matches) > 1:
        raise Malformed("%d shortcuts already start %s" % (len(matches), exe))
    if matches:
        entry = matches[0][2]
        kind, raw = field(entry, "appid")
        appid = struct.unpack("<I", raw)[0] if kind == T_INT else shortcut_appid(quoted, name)
        if kind != T_INT:
            set_field(entry, T_INT, b"appid", struct.pack("<I", appid))
    else:
        appid = shortcut_appid(quoted, name)
        index = max([int(k) for _, k, _ in shortcuts if k.isdigit()] + [-1]) + 1
        entry = [
            (T_INT, b"appid", struct.pack("<I", appid)),
            (T_STR, b"AppName", b""), (T_STR, b"Exe", b""), (T_STR, b"StartDir", b""),
            (T_STR, b"icon", b""), (T_STR, b"ShortcutPath", b""), (T_STR, b"LaunchOptions", b""),
            (T_INT, b"IsHidden", struct.pack("<I", 0)),
            (T_INT, b"AllowDesktopConfig", struct.pack("<I", 1)),
            (T_INT, b"AllowOverlay", struct.pack("<I", 1)),
            (T_INT, b"OpenVR", struct.pack("<I", 0)),
            (T_INT, b"Devkit", struct.pack("<I", 0)),
            (T_STR, b"DevkitGameID", b""),
            (T_INT, b"DevkitOverrideAppID", struct.pack("<I", 0)),
            (T_INT, b"LastPlayTime", struct.pack("<I", 0)),
            (T_STR, b"FlatpakAppID", b""),
            (T_MAP, b"tags", []),
        ]
        shortcuts.append((T_MAP, str(index).encode(), entry))
    set_field(entry, T_STR, b"AppName", name.encode())
    set_field(entry, T_STR, b"Exe", quoted.encode())
    set_field(entry, T_STR, b"StartDir", ('"%s"' % start_dir).encode())
    set_field(entry, T_STR, b"LaunchOptions", launch.encode())
    if icon:
        set_field(entry, T_STR, b"icon", icon.encode())
    new = serialise(root)
    if new != data:
        if data is not None:
            backup = vdf_path + ".super-mario-land.bak"
            if not os.path.exists(backup):
                shutil.copy2(vdf_path, backup)
        write_atomic(vdf_path, new)
    return appid


ARTWORK = {"portrait.png": "{id}p.png", "landscape.png": "{id}.png", "hero.png": "{id}_hero.png",
           "logo.png": "{id}_logo.png", "icon.png": "{id}_icon.png"}


def install_artwork(grid, appid, artwork, repair):
    os.makedirs(grid, exist_ok=True)
    for src, dst in ARTWORK.items():
        target = os.path.join(grid, dst.format(id=appid))
        if os.path.exists(target) and not repair:
            continue
        shutil.copyfile(os.path.join(artwork, src), target + ".part")
        os.replace(target + ".part", target)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--start-dir", required=True)
    ap.add_argument("--artwork", required=True)
    ap.add_argument("--name", default="Super Mario Land")
    ap.add_argument("--launch-options", default="--fullscreen")
    ap.add_argument("--icon", default="")
    ap.add_argument("--user")
    ap.add_argument("--steam-root")
    ap.add_argument("--repair-artwork", action="store_true")
    ap.add_argument("--artwork-only", action="store_true")
    ap.add_argument("--list-users", action="store_true")
    a = ap.parse_args()

    roots = steam_roots(a.steam_root)
    if not roots:
        print("Steam was not found; skipped the Steam shortcut.")
        return 2
    root = roots[0]
    ids = users(root)
    if a.list_users or not ids:
        print("Steam accounts in %s: %s" % (root, ", ".join(ids) or "none (start Steam and log in once)"))
        return 0 if ids else 2
    if a.user:
        if a.user not in ids:
            print("unknown Steam account %s (have: %s)" % (a.user, ", ".join(ids)))
            return 2
        user = a.user
    elif len(ids) == 1:
        user = ids[0]
    else:
        user = most_recent_user(root, ids)
        if not user:
            print("Several Steam accounts (%s); choose one with --user ID." % ", ".join(ids))
            return 2
        print("Using the most recent Steam account %s (others: --user ID)." % user)
    config = os.path.join(root, "userdata", user, "config")
    vdf = os.path.join(config, "shortcuts.vdf")
    grid = os.path.join(config, "grid")

    if a.artwork_only:
        try:
            raw = open(vdf, "rb").read()
            entries = parse(raw)[0][2]
        except (OSError, Malformed, IndexError):
            print("No readable shortcuts.vdf; add the shortcut first.")
            return 4
        for _, _, e in entries:
            if (field(e, "Exe")[1] or b"").decode("utf-8", "replace").strip('"') == a.exe:
                kind, raw_id = field(e, "appid")
                if kind == T_INT:
                    install_artwork(grid, struct.unpack("<I", raw_id)[0], a.artwork, True)
                    print("Artwork repaired.")
                    return 0
        print("No Super Mario Land shortcut found.")
        return 4

    if steam_running():
        print("Steam is running. Exit Steam (Steam menu > Exit), then run the installer again\n"
              "to add Super Mario Land to the Steam library.")
        return 3
    try:
        appid = update_shortcuts(vdf, a.exe, a.start_dir, a.name, a.launch_options, a.icon)
    except Malformed as e:
        print("Left shortcuts.vdf untouched: %s." % e)
        return 4
    except RuntimeError:
        print("Steam started while installing; nothing was changed. Exit Steam and try again.")
        return 3
    install_artwork(grid, appid, a.artwork, a.repair_artwork)
    print("Added \"%s\" to the Steam library of account %s (shortcut %d)." % (a.name, user, appid))
    return 0


if __name__ == "__main__":
    sys.exit(main())
