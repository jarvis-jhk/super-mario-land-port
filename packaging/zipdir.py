#!/usr/bin/env python3
"""zipdir.py OUT.zip DIR: zips DIR's contents (paths relative to DIR), keeping
Unix permissions so launchers and binaries stay executable."""
import os
import sys
import zipfile

out, base = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(base):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(root, name)
            info = zipfile.ZipInfo.from_file(path, os.path.relpath(path, base))
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f:
                z.writestr(info, f.read())
