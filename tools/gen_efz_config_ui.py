#!/usr/bin/env python3

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTRACTED = ROOT / "extracted"
SRC = ROOT / "src"

DLG_W, DLG_H = 301, 228
X_OFF, Y_OFF = 3, 16
PAD_ROW = 10
FONT = 'FONT 9, "MS PGothic", 0, 0, 128'

IDC_OK, IDC_CANCEL, IDC_START = 1000, 1001, 1114
IDC_RESOLUTION, IDC_PRESET = 1080, 1122
DEAD_IDS = set(range(1040, 1112)) - {IDC_RESOLUTION}

STATIC_REMAP = {
    (10, 125, "Static"): 1300,
    (10, 80, "Static"): 1301,
    (10, 110, "Static"): 1302,
    (10, 95, "Static"): 1303,
    (10, 140, "Static"): 1304,
    (150, 159, "Static"): 1305,
    (4, 68, "Button"): 1306,
    (148, 4, "Button"): 1307,
}

PLACED = {
    IDC_PRESET: (8, 186, 65, 90),
    IDC_RESOLUTION: (80, 187, 112, 12),
    1305: (161, 106, 131, 8),
    IDC_START: (126, 208, 64, 16),
    IDC_OK: (194, 208, 48, 16),
    IDC_CANCEL: (245, 208, 52, 16),
}

SUNKEN = 0x200
BTN, GROUP = 0x40010000, 0x40000007
ADDED = [
    ("", 1252, "Static", 0x50020080, 11, 78, 134, 8, 0),
    ("", 1253, "Static", 0x50020080, 155, 78, 135, 8, 0),

    ("", 1265, "ListBox", 0x40010101, 7, 22, 58, 112, SUNKEN),
    ("", 1266, "SysListView32", 0x4001800D, 68, 22, 226, 112, SUNKEN),
    ("", 1267, "Edit", 0x40000844, 12, 146, 277, 38, 0),
    ("", 1269, "Button", GROUP, 7, 136, 287, 52, 0),
    ("", 1264, "Button", BTN, 231, 189, 63, 13, 0),
    ("", 1268, "Static", 0x40020000, 7, 191, 220, 8, 0),

    ("", 1270, "SysListView32", 0x4001800D, 7, 22, 220, 112, SUNKEN),
    ("", 1272, "Button", BTN, 231, 22, 63, 14, 0),
    ("", 1273, "Button", BTN, 231, 38, 63, 14, 0),
    ("", 1274, "Button", BTN, 231, 60, 63, 14, 0),
    ("", 1275, "Button", BTN, 231, 76, 63, 14, 0),
    ("", 1276, "Button", BTN, 231, 98, 63, 14, 0),
    ("", 1279, "Button", BTN, 231, 120, 63, 14, 0),
    ("", 1278, "Edit", 0x40000844, 12, 146, 277, 38, 0),
    ("", 1277, "Button", GROUP, 7, 136, 287, 52, 0),
    ("", 1271, "Static", 0x40020000, 7, 191, 287, 8, 0),
    ("Aa", 1251, "Button", 0x50010000, 4, 208, 52, 16, 0),
    ("", 1254, "Static", 0x50000300, 60, 208, 65, 16, 0),
    ("", 1250, "SysTabControl32", 0x54010000, 2, 2, 297, 202, 0),
]

POPUPS = """
998 DIALOGEX 0, 0, 200, 48
STYLE 0x90C808C0
CAPTION "Bind Revival hotkey"
%(font)s
BEGIN
    CONTROL "", 1280, "Static", 0x50020000, 6, 6, 188, 36
END

997 DIALOGEX 0, 0, 200, 58
STYLE 0x90C808C0
CAPTION "Change setting"
%(font)s
BEGIN
    CONTROL "", 1284, "Static", 0x50020080, 6, 6, 188, 8
    CONTROL "", 1285, "Edit", 0x50810080, 6, 17, 188, 13
    CONTROL "OK", 1, "Button", 0x50010001, 90, 38, 50, 14
    CONTROL "Cancel", 2, "Button", 0x50010000, 144, 38, 50, 14
END
"""

AUTHORED = {
    "tab_keys": ("Keys", "キー"),

    "tab_revival": ("Revival", "Revival"),
    "tab_mods": ("Mods", "MOD"),
    "tab_revival_unavailable": ("Revival (not detected)", "Revival (未検出)"),
    "lang_toggle": ("Japanese", "English"),
    "dpad_name": ("D-Pad", "Dパッド"),
    "none_name": ("NONE", "なし"),
    "pad_none": ("(no controller)", "(コントローラーなし)"),
    "rev_col_setting": ("Setting", "項目"),
    "rev_col_value": ("Value", "値"),
    "rev_hint_edit": (
        "Double-click a setting (or press Enter) to change it.",
        "項目をダブルクリック（またはEnter）で変更します。"),
    "modset_hint": ("%s: double-click a setting (or press Enter) to change it.",
                    "%s: 項目をダブルクリック（またはEnter）で変更します。"),
    "modset_save_failed": ("Could not write the settings of %s.",
                           "%s の設定を書き込めませんでした。"),
    "btn_settings": ("Settings...", "設定..."),
    "btn_back": ("Back to mods", "MOD一覧へ"),
    "msg_caption": ("EfzConfig+", "EfzConfig+"),
    "rev_readonly": (
        "A netplay session is temporarily overriding Protocol. It cannot be changed until the session ends.",
        "ネットプレイ中は Protocol が一時的に変更されています。"
        "セッション終了まで変更できません。"),
    "rev_section_missing": (
        "Section [%s] is not present in EfzRevival.ini.",
        "セクション [%s] が EfzRevival.ini にありません。"),
    "rev_save_failed": ("Could not write EfzRevival.ini.",
                        "EfzRevival.ini に書き込めませんでした。"),
    "desc_group": ("Description", "\u8aac\u660e"),
    "mods_hint": ("Checked mods are enabled. Mods higher in the list take precedence.", "\u30c1\u30a7\u30c3\u30af\u3057\u305fMOD\u304c\u6709\u52b9\u306b\u306a\u308a\u307e\u3059\u3002\u4e0a\u306b\u3042\u308bMOD\u304c\u512a\u5148\u3055\u308c\u307e\u3059\u3002"),
    "mods_save_failed": ("Could not write EfzModManager.ini.", "EfzModManager.ini \u306b\u66f8\u304d\u8fbc\u3081\u307e\u305b\u3093\u3067\u3057\u305f\u3002"),
    "mods_col_mod": ("Mod", "MOD"),
    "mods_col_type": ("Type", "\u7a2e\u985e"),
    "mods_col_status": ("Status", "\u72b6\u614b"),
    "type_cosmetic": ("Cosmetic", "\u5916\u89b3"),
    "type_tool": ("Tool", "\u30c4\u30fc\u30eb"),
    "type_gameplay": ("Gameplay", "\u30b2\u30fc\u30e0\u30d7\u30ec\u30a4"),
    "st_not_installed": ("Not installed", "\u672a\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb"),
    "st_update": ("Update: %s", "\u66f4\u65b0\u3042\u308a: %s"),
    "st_current": ("Up to date", "\u6700\u65b0"),
    "btn_up": ("Move up", "\u4e0a\u3078"),
    "btn_down": ("Move down", "\u4e0b\u3078"),
    "btn_install": ("Install", "\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb"),
    "btn_update": ("Update", "\u66f4\u65b0"),
    "btn_page": ("Download page", "\u914d\u5e03\u30da\u30fc\u30b8"),
    "btn_update_all": ("Update all", "\u3059\u3079\u3066\u66f4\u65b0"),
    "btn_import": ("Import...", "\u30a4\u30f3\u30dd\u30fc\u30c8..."),
    "import_file": ("Zip or DLL file...", "ZIP / DLL \u30d5\u30a1\u30a4\u30eb..."),
    "import_folder": ("Folder...", "\u30d5\u30a9\u30eb\u30c0\u30fc..."),
    "mods_by": ("By %s.", "\u4f5c\u8005: %s"),
    "mods_gameplay_warning": ("Gameplay mods desync online play unless both players enable them. Disable them after use.", "\u30b2\u30fc\u30e0\u30d7\u30ec\u30a4MOD\u306f\u3001\u4e21\u30d7\u30ec\u30a4\u30e4\u30fc\u304c\u6709\u52b9\u306b\u3057\u306a\u3044\u9650\u308a\u30aa\u30f3\u30e9\u30a4\u30f3\u5bfe\u6226\u3067\u540c\u671f\u305a\u308c\u3092\u8d77\u3053\u3057\u307e\u3059\u3002\u4f7f\u7528\u5f8c\u306f\u7121\u52b9\u306b\u3057\u3066\u304f\u3060\u3055\u3044\u3002"),
    "mods_checking": ("Checking for updates...", "\u66f4\u65b0\u3092\u78ba\u8a8d\u3057\u3066\u3044\u307e\u3059..."),
    "mods_downloading": ("Downloading %s...", "%s \u3092\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u3057\u3066\u3044\u307e\u3059..."),
    "mods_installed": ("%s was installed.", "%s \u3092\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u3057\u307e\u3057\u305f\u3002"),
    "mods_install_failed": ("Could not download or install %s.\nDownload it by hand and use Import.", "%s \u3092\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u307e\u305f\u306f\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u3067\u304d\u307e\u305b\u3093\u3067\u3057\u305f\u3002\n\u624b\u52d5\u3067\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u3057\u3001\u30a4\u30f3\u30dd\u30fc\u30c8\u3092\u4f7f\u7528\u3057\u3066\u304f\u3060\u3055\u3044\u3002"),
    "mods_import_failed": ("Could not import %s.", "%s \u3092\u30a4\u30f3\u30dd\u30fc\u30c8\u3067\u304d\u307e\u305b\u3093\u3067\u3057\u305f\u3002"),
    "mods_updates_prompt": ("Updates are available for:\n\n%s\nInstall them now?", "\u6b21\u306eMOD\u306b\u66f4\u65b0\u304c\u3042\u308a\u307e\u3059:\n\n%s\n\u4eca\u3059\u3050\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u3057\u307e\u3059\u304b\uff1f"),
    "mods_manager_prompt": ("EfzModManager is not installed in this folder.\nDownload and install it now?", "\u3053\u306e\u30d5\u30a9\u30eb\u30c0\u30fc\u306b EfzModManager \u304c\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u3055\u308c\u3066\u3044\u307e\u305b\u3093\u3002\n\u4eca\u3059\u3050\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u3057\u3066\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u3057\u307e\u3059\u304b\uff1f"),
    "mods_page_opened": ("The download page was opened in your browser.\nDownload the archive, then use Import or drop it on this window.", "\u914d\u5e03\u30da\u30fc\u30b8\u3092\u30d6\u30e9\u30a6\u30b6\u30fc\u3067\u958b\u304d\u307e\u3057\u305f\u3002\n\u30a2\u30fc\u30ab\u30a4\u30d6\u3092\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u3057\u3001\u30a4\u30f3\u30dd\u30fc\u30c8\u3092\u4f7f\u3046\u304b\u3001\u3053\u306e\u30a6\u30a3\u30f3\u30c9\u30a6\u306b\u30c9\u30ed\u30c3\u30d7\u3057\u3066\u304f\u3060\u3055\u3044\u3002"),
    "mods_busy": ("A download is still running.", "\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u304c\u307e\u3060\u5b9f\u884c\u4e2d\u3067\u3059\u3002"),
    "capture_title": ("Bind Revival hotkey", "Revival操作キーの設定"),
    "capture_prompt": (
        "%s\n\nPress a key, or a button on the pad that has the A button.\n"
        "Esc cancels; Backspace clears the binding.",
        "%s\n\nキー、またはAボタンを割り当てたパッドのボタンを押してください。\n"
        "Escで中止、Backspaceで解除。"),
    "edit_title": ("Change setting", "設定の変更"),
    "warn_no_pad_action": (
        "Player %d uses the D-Pad for directions but no action button is on a controller; "
        "Revival would not know which pad to read.",
        "プレイヤー%dは方向にDパッドを使いますが、"
        "アクションがコントローラーに割り当てられていません。"
        "Revivalが読み取るパッドを特定できません。"),
    "launch_failed": ("Could not start EFZ.exe.", "EFZ.exe を起動できませんでした。"),
    "dinput_failed": ("DirectInput could not be initialized.",
                      "DirectInput を初期化できませんでした。"),
}

ORIGINAL_MESSAGES = {
    "msg_open_failed": ("kMessageOpenFailed", "Could not create or open KEY.ini.\n"
                        "It may be read-only or in use by another program."),
    "msg_read_failed": ("kMessageReadFailed", "Could not read KEY.ini."),
    "msg_write_failed": ("kMessageWriteFailed", "Could not write KEY.ini."),
}

CONTROL_RE = re.compile(
    r'^\s*CONTROL\s+"((?:[^"]|"")*)"\s*,\s*(-?\d+)\s*,\s*"([^"]+)"\s*,\s*'
    r'(0x[0-9A-Fa-f]+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,?\s*$'
)

def parse_rc(path):
    controls = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        m = CONTROL_RE.match(line)
        if m:
            controls.append({
                "text": m.group(1).replace('""', '"'), "id": int(m.group(2)),
                "class": m.group(3), "style": int(m.group(4), 16),
                "x": int(m.group(5)), "y": int(m.group(6)),
                "cx": int(m.group(7)), "cy": int(m.group(8)),
            })
    return controls

def parse_bytes(header_text, array_name):

    m = re.search(re.escape(array_name) + r"\[[^\]]*\](?:\[[^\]]*\])?\s*=\s*\{(.*?)\n\};",
                  header_text, re.S)
    if not m:
        raise SystemExit("array %s not found" % array_name)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(tok, 16) for tok in re.findall(r"0x[0-9A-Fa-f]{2}", body)]

def sjis_rows(flat, width):

    rows = []
    for i in range(0, len(flat), width):
        chunk = bytes(flat[i:i + width]).split(b"\0")[0]
        rows.append(chunk.decode("cp932").strip())
    return rows

def wide_literal(text):

    parts, seg = [], []
    for ch in text:
        if ch == "\n":
            seg.append("\\n")
        elif ch in '"\\':
            seg.append("\\" + ch)
        elif 0x20 <= ord(ch) < 0x7F:
            seg.append(ch)
        else:
            if seg:
                parts.append('L"%s"' % "".join(seg))
                seg = []
            parts.append('L"\\x%04X"' % ord(ch))
    if seg or not parts:
        parts.append('L"%s"' % "".join(seg))
    return " ".join(parts)

def main():
    en = parse_rc(EXTRACTED / "config_resources_en.rc")
    ja = parse_rc(EXTRACTED / "config_resources_ja.rc")
    if len(en) != len(ja):
        raise SystemExit("template control counts differ: %d vs %d" % (len(en), len(ja)))
    ja_by_key = {(c["id"], c["x"], c["y"], c["class"]): c for c in ja}

    rc_lines = []
    control_texts = []

    for c in en:
        j = ja_by_key.get((c["id"], c["x"], c["y"], c["class"]))
        if j is None:
            raise SystemExit("no JA match for control id=%d at %d,%d" % (c["id"], c["x"], c["y"]))
        if c["id"] in DEAD_IDS:
            continue

        out_id = c["id"]
        if out_id == -1:
            out_id = STATIC_REMAP.get((c["x"], c["y"], c["class"]))
            if out_id is None:
                raise SystemExit("unmapped text static at %d,%d" % (c["x"], c["y"]))
        if c["text"]:
            control_texts.append((out_id, c["text"], j["text"]))

        if out_id in PLACED:
            x, y, cx, cy = PLACED[out_id]
        else:

            x, y, cx, cy = c["x"] + X_OFF, c["y"] + Y_OFF, c["cx"], c["cy"]
            if c["y"] >= 68:
                y += PAD_ROW
            elif out_id in (1004, 1307):
                cy += PAD_ROW
        rc_lines.append('    CONTROL "%s", %d, "%s", 0x%08X, %d, %d, %d, %d'
                        % (c["text"].replace('"', '""'), out_id, c["class"], c["style"], x, y, cx, cy))

    for text, cid, cls, style, x, y, cx, cy, exstyle in ADDED:
        rc_lines.append('    CONTROL "%s", %d, "%s", 0x%08X, %d, %d, %d, %d, 0x%X'
                        % (text, cid, cls, style, x, y, cx, cy, exstyle))

    rc_text = "\n".join([
        '#include "efz_config_resource.h"',
        "",
        "LANGUAGE 9, 1",
        "",
        'IDI_CONFIG ICON "../assets/config_icon_new.ico"',
        'IDR_CATALOGUE RCDATA "../mods_catalogue.csv"',
        "",
        "999 DIALOGEX 0, 0, %d, %d" % (DLG_W, DLG_H),
        "STYLE 0x90C808C0",
        'CAPTION "EfzConfig+"',
        FONT,
        "BEGIN",
    ] + rc_lines + ["END"]) + "\n" + POPUPS % {"font": FONT}
    (SRC / "efz_config_resources.rc").write_text(rc_text, encoding="ascii", newline="\r\n")

    data_en = (EXTRACTED / "config_extracted_data_en.h").read_text(encoding="utf-8-sig", errors="replace")
    data_ja = (EXTRACTED / "config_extracted_data_ja.h").read_text(encoding="utf-8-sig", errors="replace")

    strings = dict(AUTHORED)
    for key, (symbol, english) in ORIGINAL_MESSAGES.items():
        strings[key] = (english, sjis_rows(parse_bytes(data_ja, symbol), 10 ** 6)[0])

    keyboard = sjis_rows(parse_bytes(data_en, "kKeyboardBindingNames"), 7)
    pads = [sjis_rows(parse_bytes(data_en, "kJoystick%dBindingNames" % n), 8) for n in (1, 2)]
    presets = list(zip(sjis_rows(parse_bytes(data_en, "kPresetNames"), 16),
                       sjis_rows(parse_bytes(data_ja, "kPresetNames"), 16)))
    default_image = parse_bytes(data_en, "kOriginalDefaultKeyIniImage")
    if len(keyboard) != 256 or len(default_image) != 157:
        raise SystemExit("unexpected extracted table sizes")

    h = [
        "#ifndef EFZ_CONFIG_TABLES_H",
        "#define EFZ_CONFIG_TABLES_H",
        "",
        "typedef struct LangText { int id; const wchar_t *text[2]; } LangText;",
        "",
        "static const LangText kLangControlTexts[] = {",
    ]
    h += ["    { %d, { %s, %s } }," % (cid, wide_literal(e), wide_literal(j))
          for cid, e, j in control_texts]
    h += ["};", "",
          "static const wchar_t *const kLangPresetNames[4][2] = {"]
    h += ["    { %s, %s }," % (wide_literal(e), wide_literal(j)) for e, j in presets]
    h += ["};", "", "enum LangString {"]
    h += ["    LS_%s," % k.upper() for k in strings]
    h += ["};", "static const wchar_t *const kLangStrings[][2] = {"]
    h += ["    { %s, %s }," % (wide_literal(e), wide_literal(j))
          for k, (e, j) in strings.items()]
    h += ["};", "",
          "static const wchar_t *const kKeyboardNames[256] = {"]
    h += ["    %s," % wide_literal(n) for n in keyboard]
    h += ["};", "static const wchar_t *const kJoystickNames[2][32] = {"]
    for names in pads:
        h.append("    { " + ", ".join(wide_literal(n) for n in names) + " },")
    h += ["};", "",
          "static const unsigned char kDefaultKeyIni[157] = {"]
    for i in range(0, len(default_image), 12):
        h.append("    " + " ".join("0x%02X," % b for b in default_image[i:i + 12]))
    h += ["};", "", "#endif", ""]
    (SRC / "efz_config_tables.h").write_text("\n".join(h), encoding="ascii", newline="\n")

    print("wrote efz_config_resources.rc (%d controls) and efz_config_tables.h (%d captions, %d strings)"
          % (len(rc_lines), len(control_texts), len(strings)))

if __name__ == "__main__":
    main()
