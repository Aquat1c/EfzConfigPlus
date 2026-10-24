#!/usr/bin/env python3

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SDK_CANDIDATES = [
    Path(r"C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include\dinput.h"),
    Path(r"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\dinput.h"),
]

DIK_RE = re.compile(r'^#define\s+(DIK_[A-Za-z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)\s*(?:/\*.*)?$')

def main():
    header_path = next(p for p in SDK_CANDIDATES if p.is_file())
    names = {}
    for line in header_path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = DIK_RE.match(line)
        if not m:
            continue
        name, value = m.group(1), int(m.group(2), 0)
        if 0 <= value <= 255 and value not in names:
            names[value] = name

    if not names:
        raise SystemExit("no DIK_ defines parsed from %s" % header_path)

    lines = []
    lines.append("#ifndef EFZ_CONFIG_DIK_H")
    lines.append("#define EFZ_CONFIG_DIK_H")
    lines.append("")
    lines.append("static const char *const kDikNames[256] = {")
    for code in range(256):
        name = names.get(code)
        if name is not None:
            lines.append("    [%d] = \"%s\"," % (code, name))
        elif code in (0x79, 0x7A, 0x82, 0x8B, 0x92, 0x9B, 0x98):

            lines.append("    [%d] = NULL," % code)
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    (ROOT / "src" / "efz_config_dik.h").write_text("\n".join(lines), encoding="ascii", newline="\n")
    print("wrote efz_config_dik.h with %d names from %s" % (len(names), header_path))

if __name__ == "__main__":
    main()
