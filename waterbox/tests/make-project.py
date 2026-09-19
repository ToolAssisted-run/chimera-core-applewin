#!/usr/bin/env python3
"""Writes the .chimeraProject that run-frontend.sh opens headless.

A project is what Chimera actually hands the core: the files in their slots
(order = swap order), a value for every declared setting, and an input log in
the machine's own button names. The disk in drive 1 arrives through the
"slots" file, which the bare-image leg never exercises.

usage: make-project.py <package> <out.chimeraProject> <frames> <slot=file> [<slot=file> ...]
"""
import hashlib
import json
import os
import sys
import zipfile


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def main():
    package, out, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
    z = zipfile.ZipFile(package)
    cfg = json.loads(z.read("waterbox.config"))
    inputs = cfg["input"]
    buttons = inputs["buttons"]
    axes = inputs.get("axes", [])

    # the log mnemonic (see chimera-core-ares's make-project.py): grouped by
    # player, axes first as five-wide values with a comma, one char per button
    def player_of(name):
        if len(name) > 2 and name[0] in "Pp" and name[1].isdigit():
            return int(name[1])
        return 0

    groups = max([player_of(a["name"]) for a in axes] + [player_of(b) for b in buttons] + [0]) + 1
    row, key = "", ""
    for g in range(groups):
        row += "|"
        key += "#"
        for a in axes:
            if player_of(a["name"]) == g:
                row += "%5d," % a.get("neutral", 0)
                key += a["name"] + "|"
        for b in buttons:
            if player_of(b) == g:
                row += "."
                key += b + "|"
    row += "|"
    log = "[Input]\nLogKey:" + key + "\n" + "\n".join([row] * frames) + "\n[/Input]\n"

    files = []
    for arg in sys.argv[4:]:
        slot, path = arg.split("=", 1)
        files.append({"name": os.path.basename(path), "sha1": sha1(path), "slot": slot})

    settings = {d["name"]: d.get("default") for d in cfg.get("settings", [])}
    settings = {k: v for k, v in settings.items() if v is not None}

    # the firmware this machine needs, pinned as the wizard would pin it: every
    # declaration whose condition the settings meet (the ROMs themselves come
    # from --firmware on the command line, by id)
    def needed(decl):
        when = decl.get("requiredWhen")
        return when is None or settings.get(when["setting"]) in when.get("in", [])
    firmware = [{"id": d["id"], "sha1": d["sha1"]} for d in cfg.get("firmware", []) if needed(d)]

    project = {
        "id": "applewin-gate-01",
        "title": "Apple II through Chimera",
        "description": "written by waterbox/tests/run-frontend.sh",
        "core": {"name": cfg["coreName"], "version": cfg["version"], "sha1": sha1(package)},
        "rerecords": 0,
        "files": files,
        "settings": settings,
        "firmware": firmware,
        "coreCache": [],
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": cfg["systemId"],
            "SHA1": files[0]["sha1"] if files else "",
            "LastInputFrame": str(frames - 1),
            "VsyncNumerator": str(cfg["video"]["vsyncNumerator"]),
            "VsyncDenominator": str(cfg["video"]["vsyncDenominator"]),
        },
    }
    with open(out, "w") as f:
        json.dump(project, f, indent="\t")
    return 0


if __name__ == "__main__":
    sys.exit(main())
