# Updates and changelog

Every release is a GitHub release `v<version>` (see packaging/make_release.sh).
Besides the packages it carries one update file per package and `update.txt`:

    version 0.1.0
    file <asset> <size in bytes> <sha256>

| Package | Replaced file | Update asset |
|---------|---------------|--------------|
| AppImage (Linux, Steam Deck) | `$APPIMAGE` | `Super_Mario_Land_Port-<arch>.AppImage` |
| Linux tarball | the running `sml` | `sml-linux-<arch>` |
| Windows zip | the running `sml.exe` (renamed to `.old`, removed next start) | `sml-windows-x86_64.exe` |
| PortMaster | `ports/supermarioland/sml.aarch64` | `sml-portmaster-aarch64` |

At start-up (not headless, not with `[update] check = no`, `--no-update` or
`SML_NO_UPDATE=1`, never in development builds, and not in installs of the
Warpzone launcher, which updates them itself: `WARPZONE_LAUNCHED=1`, or a
`.warpzone-managed` file next to the program or one folder above it), `src/platform/update.c` starts
a background thread that runs curl (Windows 10+ ships `curl.exe`):

1. the release list from the GitHub API; the highest `v<version>` tag wins
   (pre-releases included);
2. if it is newer than the running version, that release's `update.txt`;
3. the package's update file to `<program>.new` in the program's directory;
4. size and SHA-256 must match `update.txt`, else the file is deleted;
5. an atomic rename replaces the program; the running process keeps the old
   file. The player sees "Version X installed - restart to play it".

Offline, missing curl or GitHub errors end the check silently. A read-only
install only shows "Version X is available". The game never waits for the
check and it changes no game state. Settings, saves, the ROM and the Steam
shortcut are separate files and are never touched; the Steam shortcut points to
the AppImage path, which stays the same, so its app ID and artwork stay.
`SML_UPDATE_URL=file:///dir` points the check at a local test tree
(`dir/releases.json`, `dir/download/v<version>/...`).

## Changelog

CHANGELOG.md is built into the program. On start-up the frontend compares the
version with `last_version.txt` in the data directory and shows the sections
newer than the last seen version (only the current one on a first install)
before the game starts; `--changelog` shows all sections up to the running
version. The screen uses the frontend's own 5x7 font (src/platform/text.c).
