#!/usr/bin/env python3
"""The wire, twice: waterbox.config declares the buttons and axes by name, the
driver's AwButton/AwAxis enums number them. This checks they are the same
list in the same order, so a rename on one side cannot silently shift every
key on the other."""
import json
import re
import sys

root = sys.argv[1]
cfg = json.load(open(f"{root}/waterbox/waterbox.config"))
src = open(f"{root}/waterbox/applewin-driver.h").read()


def enum_names(name):
    body = re.search(r"enum %s\s*\{(.*?)\};" % name, src, re.S).group(1)
    names = []
    for token in re.split(r"[,\n]", body):
        token = token.split("/*")[0].split("=")[0].strip()
        if token and not token.endswith("_COUNT"):
            names.append(token)
    return names


buttons = enum_names("AwButton")
axes = enum_names("AwAxis")
cfg_buttons = cfg["input"]["buttons"]
cfg_axes = [a["name"] for a in cfg["input"]["axes"]]
if len(buttons) != len(cfg_buttons):
    sys.exit(f"driver has {len(buttons)} buttons, waterbox.config {len(cfg_buttons)}")
if len(axes) != len(cfg_axes):
    sys.exit(f"driver has {len(axes)} axes, waterbox.config {len(cfg_axes)}")


def normalise(s):
    return re.sub(r"[^A-Z0-9]", "", s.upper())


# the enum abbreviates a few of the config's words
short = {"LEFTBRACKET": "LBRACKET", "RIGHTBRACKET": "RBRACKET"}
for i, (d, c) in enumerate(zip(buttons, cfg_buttons)):
    dn = normalise(d.replace("AW_BTN_", ""))
    cn = normalise(c)
    cn = short.get(cn.replace("KEY", "", 1), cn)
    # "Key X" in the config is "X" in the enum; the joystick and disk
    # buttons are spelt the same on both sides
    if cn != dn and cn != "KEY" + dn:
        sys.exit(f"button {i}: driver {d} vs config '{c}'")
for i, (d, c) in enumerate(zip(axes, cfg_axes)):
    dn = normalise(d.replace("AW_AXIS_", ""))
    cn = normalise(c).replace("JOYSTICK", "")
    if cn != dn:
        sys.exit(f"axis {i}: driver {d} vs config '{c}'")
print(f"{len(buttons)} buttons and {len(axes)} axes agree")
