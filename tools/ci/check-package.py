#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the release archives written by CPack: usage check-package.py BUILD_DIR VERSION."""
import glob
import sys
import tarfile
import zipfile


def names(path):
    if path.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            return [n.split("/", 1)[-1] if "/" in n else n for n in z.namelist()]
    with tarfile.open(path) as t:
        return [n.split("/", 1)[-1] if "/" in n else n for n in t.getnames()]


def main():
    build, version = sys.argv[1], sys.argv[2]
    archives = sorted(glob.glob(f"{build}/metroplex3k-{version}-*.tar.gz") + glob.glob(f"{build}/metroplex3k-{version}-*.zip"))
    app = [a for a in archives if "-tools" not in a]
    tools = [a for a in archives if "-tools" in a]
    ok = True
    if len(app) != 1 or len(tools) != 1:
        print(f"FAIL: expected one app and one tools archive for {version}, found {archives}")
        return 1
    n = names(app[0])
    base = {x.rsplit("/", 1)[-1] for x in n}
    need = {"LICENSE", "README.md", "CHANGELOG.md", "USER-GUIDE.md"}
    need.add("metroplex3k.exe" if app[0].endswith(".zip") and "Windows" in app[0] else "metroplex3k")
    if "Windows" in app[0]:
        need.add("SDL2.dll")
    for want in sorted(need):
        if want not in base:
            print(f"FAIL: {app[0]} lacks {want}")
            ok = False
    t = {x.rsplit("/", 1)[-1] for x in names(tools[0])}
    for want in ("diag", "conformance", "probe_verify"):
        if not ({want, want + ".exe"} & t):
            print(f"FAIL: {tools[0]} lacks {want}")
            ok = False
    # No game data may ever be packaged.
    bad = [x for x in n if x.lower().endswith((".ixf", ".dat", ".xa", ".tgq", ".sc3", ".pak"))]
    if bad:
        print(f"FAIL: game data in the package: {bad}")
        ok = False
    print(f"{app[0]}: {len(n)} files; {tools[0]}: {len(t)} files")
    print("package check: ok" if ok else "package check: FAILED")
    return 0 if ok else 1


sys.exit(main())
