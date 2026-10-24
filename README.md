# EfzConfig+

A replacement for Eternal Fighter Zero's `config.exe` that also edits
EfzRevival's settings and manages mods. It runs on Windows XP and newer and
keeps the look of the original tool.

Put `EfzConfig+.exe` in the game folder, next to `EFZ.exe`.

## Keys

The original key editor: 1P/2P bindings, four presets, game options, and
Start Game. It reads and writes the same `KEY.ini` as the original.

- Click a slot, then press a key, a pad button or a stick direction. The
  selection moves on to the next slot of the same player and stops after
  that player's last button. Nothing is bound until you click a slot.
- Press a D-Pad direction on a direction slot to use the D-Pad for that
  player. All four directions switch at once, because Revival only reads the
  D-Pad when no direction is bound to the stick.
- The detected controllers are shown under the 1P and 2P boxes; each box
  names the controller its player is bound to. PAD1 and
  PAD2 are the same two controllers the game will use.
- A player uses one controller. Press anything on the other controller
  while binding a player and that player moves to it; if the other player
  was on that controller, the two trade places.
- The D-Pad is read from the controller that the player's first pad button
  (A, then B, C, D) is on, so bind at least one button on that controller.
- With EfzRevival 1.02h or newer, saving while the game is running applies
  the first preset's keys right away. The other presets apply the next time
  the game starts.

## Revival

Shown when `EfzRevival.ini` is in the game folder. Pick a section on the
left, then double-click a setting (or press Enter) to change it:

- hotkeys are captured from the keyboard or from a pad button (Backspace
  clears one). Revival reads pad hotkeys from one controller only: the one
  1P's A button is on, or 2P's if 1P plays on the keyboard,
- on/off and list settings cycle through their values,
- numbers and text open a small edit box.

The description box shows the notes Revival wrote above each setting. Only
the values you change are rewritten; the rest of the file stays as it was.

## Mods

Mods are loaded by [EfzModManager](https://github.com/fishshapedfish/EfzModManager).
If the game folder does not have it yet, opening the Mods tab offers to
download and install it.

- **Enable / disable**: tick or untick a mod.
- **Order**: mods higher in the list win when two mods change the same
  thing. Use Move up / Move down.
- **Install / Update**: mods with releases on GitHub are downloaded and
  installed for you. When the tool starts it checks for newer releases and
  offers to install them. Other mods open their download page. A mod you
  installed by hand is recognised as up to date when its DLL is identical
  (SHA-256) to the one in the latest release, so it is not downloaded again.
- **Import**: installs a `.zip`, a folder or a `.dll` you downloaded
  yourself. Dropping files on the window does the same.
- **Settings**: opens the selected mod's own settings (see below).

Gameplay mods desync online play unless both players enable them.

The list of known mods is [mods_catalogue.csv](mods_catalogue.csv) in this
repository. The tool downloads the current copy when it starts, so new mods
and changed download links appear without a new `EfzConfig+.exe`. A copy is
built into the exe for when there is no connection.

Downloads need a modern Windows. On Windows XP the built-in downloader
cannot reach GitHub; download the mod on another machine and use Import.

### Adding a mod to the list

The list is the file `mods_catalogue.csv` in this repository. To get your mod
into it, open a pull request that adds one row to that file, or open an
issue with the details below and it will be added for you. Once the change
is merged, every copy of the tool shows the mod at its next start.

| Column | Meaning |
|---|---|
| Name | Name shown in the list |
| Type | `Cosmetic`, `Tool` or `Gameplay` |
| Description | One or two sentences |
| Created By | Author |
| Download | A GitHub repository link, or any other link (opened in the browser) |
| Folder | Optional: the mod's folder name under `mods\` when it differs from Name |

For a GitHub link the tool installs the latest release from its `.zip` asset,
unpacked into `mods\<Folder>` (or from its `.dll` asset when the release has
no zip). Put everything the mod needs in the zip, and name the DLL after the
mod's folder. Existing `.ini` files are never replaced, so users keep their
settings across updates.

### Settings page for your mod

A mod gets a Settings page by exporting a table of its settings from its
DLL. Copy [include/efz_config_api.h](include/efz_config_api.h) into your mod
and define the table:

```c
#include "efz_config_api.h"

static const EfzConfigSetting kSettings[] = {
    { "Display", "Fullscreen", NULL, "Start in fullscreen.",
      EFZ_SETTING_SWITCH, 0, 0, NULL, "0" },
    { "Display", "Sharpness", NULL, "From 10 (soft) to 40 (hard).",
      EFZ_SETTING_NUMBER, 10, 40, NULL, "20" },
    { "Display", "Filter", NULL, "Scaling filter.",
      EFZ_SETTING_CHOICE, 0, 0, "Linear|Point|Sharp", "Linear" },
};
EFZ_CONFIG_EXPORT const EfzConfigSchema EFZ_ConfigSchema = {
    EFZ_CONFIG_SCHEMA_VERSION, sizeof(EfzConfigSetting),
    "MyMod.ini", sizeof(kSettings) / sizeof(kSettings[0]), kSettings,
};
```

Each entry names the ini section and key, an optional label and
description, the kind of value (switch, number with a range, list of
choices, hotkey or text) and the value used when the key is missing. The
values are stored in the ini file named in the table, next to the DLL, so
the mod keeps reading its settings the way it already does. The file is
created when the first setting is changed.

EfzConfig+ reads the table from the DLL file without running the mod, so it
has to be plain constant data.

A mod without the table still gets a Settings page when it ships
`mods\<Name>\<Name>.ini`: sections become the list on the left, the `;`
comment lines above a key become its description, and comment lines of the
form `; Value - text` or `; Value = text` directly above a key list its
choices.

Mod settings are saved when you leave the page or press OK.

## Linux

The tool runs under Wine. Wine normally ignores the `dinput.dll` (the mod
loader) and `DDRAW.dll` (EfzRevival) in the game folder and uses its own, so
mods and Revival would not load. When EfzConfig+ runs under Wine it tells
the prefix to take both from the game folder for `EFZ.exe` (the same
setting winecfg's Libraries tab makes). It applies however the game is
started afterwards. A choice you already made for these two files is left
as it is, and a `WINEDLLOVERRIDES` variable set by your launcher still has
the last word.

Run `EfzConfig+.exe` once in the prefix you play in. Japanese text needs a
Japanese font installed in the prefix or on the system.

## Language

The button in the bottom left switches between English and Japanese. The
choice is remembered per Windows user. The version next to it opens the
page of the latest release when clicked.

## Building

Requires Visual Studio with the `v141_xp` toolset and CMake.

```
cmake -B build-xp -A Win32 -T v141_xp
cmake --build build-xp --config Release
ctest --test-dir build-xp -C Release
```

The result is `build-xp\Release\EfzConfig+.exe`.

| Path | Contents |
|---|---|
| `src/` | the tool; `efz_config_resources.rc`, `efz_config_tables.h` and `efz_config_dik.h` are generated and checked in |
| `include/` | `efz_config_api.h`, the header mods use to describe their settings |
| `tools/` | the generators (`gen_efz_config_ui.py`, `gen_efz_config_dik.py`) and the tests. The UI generator needs data extracted from the original `config.exe` and `config_EN.exe`, which is not included here |
| `docs/` | `HOW_IT_WORKS.md`, a detailed description of everything the tool does |
| `assets/` | the icon |
| `third_party/miniz/` | zip reader (public domain) |
| `mods_catalogue.csv` | the mod list |

## License

The code in this repository is MIT licensed, see [LICENSE](LICENSE).
