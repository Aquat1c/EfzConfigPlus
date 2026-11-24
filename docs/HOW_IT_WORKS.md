# How EfzConfig+ works

EfzConfig+ replaces the configuration program that ships with Eternal Fighter
Zero. It is a single exe that sits in the game folder next to `EFZ.exe`. It
has three pages: Keys, Revival and Mods.

## Files the tool reads and writes

Everything is found relative to the folder the exe is in.

| File | Used for |
| --- | --- |
| `KEY.ini` | Key bindings and game options. Always read and written. |
| `EFZ.exe` | Started by the Start Game button. |
| `EfzRevival.ini` | The Revival page. The page is only available when this file exists. |
| `EfzModManager.ini` | The Mods page: which mods are enabled and in what order. |
| `mods\<Folder>\` | One folder per mod. |
| `mods\<Folder>\efz_config.release` | Written by the tool after it installs a release: the release tag and the mod's list name. |

Outside the game folder the tool keeps two things:

- The language choice, in the registry under `HKCU\Software\EfzConfig+`.
- The downloaded copy of the mod list, in `%APPDATA%\EfzConfig+`.

The tool never loads a DLL from the game folder. The `dinput.dll` there is the
mod loader, and loading it would load every mod into the configuration tool.
DirectInput is loaded from the Windows system folder by its full path, and mod
DLLs are only ever opened as data (see "Settings pages for mods").

## The Keys page

### What KEY.ini looks like

`KEY.ini` is not a text file. It is 157 bytes: four presets of 39 bytes each,
followed by one byte that says which preset is selected.

A preset is 16 bindings of two bytes each, then seven option bytes.

The 16 bindings are player 1 first, then player 2, each in the order Up, Down,
Left, Right, A, B, C, D. A binding is a device byte and a code byte:

- Device 0 is the keyboard, and the code is the DirectInput scan code of the
  key.
- Device 1 is the first controller and device 2 is the second. Codes 1 to 16
  are buttons. Codes 17, 18, 19 and 20 are the stick pushed up, down, left and
  right.
- Device 0 with code 0 means "nothing bound".

The seven option bytes are, in order: start at 640x480, CPU difficulty, draw
rate, effects, damage, skip the opening movie, and round count. The game
reads the options from the first preset, so the tool writes the same options
into all four presets when it saves. The "skip the opening movie" byte has no
control in the window and is left as it was.

When `KEY.ini` does not exist the tool creates it from the same default image
the original program uses. A file shorter than 157 bytes is accepted: the part
it lacks keeps the default values. If the selected-preset byte is out of
range it is treated as the first preset, because the game reads that byte
without checking it.

### Which controllers are PAD1 and PAD2

The game asks DirectInput for attached joysticks and takes the first two that
it can fully set up: the device must accept the standard joystick data format
and must have an X and a Y axis. The tool sets up devices in exactly the same
way, so the two controllers it shows are the two the game will use, in the
same order. A device the game would reject is skipped by the tool as well.

That order only decides which physical controller is called PAD1 and which
PAD2. Which of the two a player actually plays on is decided by the bindings:
every binding in `KEY.ini` says "first controller" or "second controller".
Player 1 can be bound to PAD2 and player 2 to PAD1.

Under each player's box the tool shows one of the two controllers. The line
names the controller that player is bound to. A player with no controller
binding gets the controller the other player leaves free, so that both
detected controllers stay visible.

### Binding

Nothing is selected when the tool opens or when you change the preset, and
input is ignored until you click a slot. This is deliberate: the original
program started listening immediately, which made it easy to rebind Up by
accident.

After a slot is clicked, the next key press, controller button or stick
direction is stored in it and the selection moves to the next slot of the same
player. After a player's last button the selection clears. It does not run on
into the other player's slots as the original did.

Details of the capture:

- Only a fresh press counts. After every capture, and whenever a slot is
  picked, the tool waits until every key, button, stick and D-Pad is released
  before it listens again. Something you were already holding is never taken.
- A stick direction is taken when the stick passes three quarters of its
  travel and is considered released when it falls back inside about one third.
- A binding lives in one slot only. Binding something that is already used
  elsewhere in the preset empties the other slot.
- These keys cannot be bound, as in the original: Esc, Kanji, F1 to F15,
  Enter, numpad Enter, Tab and both Shift keys.
- Only the first 16 buttons of a controller can be bound.

### Triggers and the right stick

The game reads sixteen buttons and the left stick of a controller and nothing
else. The triggers of Xbox-type controllers and the right stick of any
controller are axes it never looks at. The ControllsFix mod makes them appear
to the game as buttons: by default the left and right trigger are buttons 11
and 12 and the right stick's up, down, left and right are buttons 13 to 16.

The tool contains the same rules as the mod, compiled from the same source
files, and applies them to every controller it reads. Pulling a trigger while
a slot is selected is therefore captured as the button the game will see with
the mod running. The numbers come from `mods\ControllsFix\ControllsFix.ini`
when that file exists and are read again whenever a mod's settings are saved,
so a change made on the mod's Settings page is followed at once.

Such a button only exists in the game while the mod is loaded, so the tool
checks before it stores one:

- The mod is installed and enabled: the button is stored like any other.
- There is no EfzModManager in the game folder: a message says that the input
  needs the mod and the mod needs the mod loader. Nothing is stored.
- Otherwise the tool asks whether to set the mod up. On No nothing is stored.
  On Yes, a mod that is already in the `mods` folder is ticked in the mod list
  and the button is stored; the tick is written with OK like any other change
  on the Mods page. A mod that is not there is downloaded and installed from
  its entry in the mod list, the same way as with the Install button, and the
  button is stored. If the list has no entry for it or another download is
  running, the usual "could not install" message appears and nothing is
  stored.

When nothing was stored the slot stays selected, and the tool waits for
everything to be released before it listens again.

A button that the controller really has is never questioned, even when the mod
would produce the same number. A right stick direction can go into a direction
slot as well as a button slot, since to the game it is a button.

In the hotkey popup of the Revival page these inputs are only accepted while
the mod is installed and enabled; otherwise they are ignored there.

### The D-Pad

EfzRevival reads a controller's D-Pad itself. It does so for a player only
when none of that player's four directions has anything bound, and it reads
the D-Pad of the controller that the player's first controller-bound button is
on, looking at A, then B, then C, then D.

The tool follows from that:

- Pressing a D-Pad direction while a direction slot is selected empties all
  four directions of that player and moves the selection to A. The four slots
  then read "D-Pad". Emptying only one direction would be useless, because
  Revival ignores the D-Pad as long as any direction is bound.
- Pressing the D-Pad while a button slot is selected does nothing. The game
  cannot use a D-Pad direction as a button.
- A D-Pad press leaves no trace in `KEY.ini` of which controller it came
  from. Until one of the player's buttons is bound to a controller, the tool
  remembers the controller the D-Pad was pressed on and names it in the line
  under that player's box. This memory lasts until the preset is changed or
  the tool is closed.
- When you save and a player uses the D-Pad but has no button on any
  controller, the tool tells you, because Revival would then not know which
  controller to read.

### One controller per player

A player uses one controller. If you press something on the other controller
while binding a player who already has bindings on the first one, the whole
preset is flipped: every binding on the first controller moves to the second
and the other way round. The effect is that the player moves to the
controller you just touched, and if the other player was using that
controller the two trade places. Keyboard bindings are not affected.

### Saving

OK saves and closes. Cancel closes without saving. Start Game saves and then
starts `EFZ.exe` from the game folder; if that fails the tool says so and
stays open.

With EfzRevival 1.02h or newer, a save made while the game is running is
picked up by the game immediately, but only for the first preset.

## The Revival page

The page edits `EfzRevival.ini`. The sections of the file are listed on the
left and the settings of the selected section on the right. Section and key
names are Revival's own and are not translated; a key such as `MaxRollback` is
shown as "Max Rollback".

### How the file is edited

`EfzRevival.ini` is treated as a list of lines. When a value changes, only the
value part of that one line is rewritten. The encoding, the byte order mark,
every line ending (files with mixed line endings exist), the spacing around
`=`, the comments and anything after the value stay exactly as they were. The
file is written to a temporary file first and then moved over the original, so
a failed write cannot leave a half-written file behind. Nothing is written at
all unless a setting was changed.

The same rules apply to `EfzModManager.ini` and to the ini files of mods.

### What kind of value a setting is

The tool decides how to edit a value first from the key's name and then from
its current value:

- A known list of choices, for example `Protocol` (IPv4 or IPv6) or the menu
  font, is cycled through.
- A value that is a key or button name is a hotkey and opens the capture
  window.
- A whole number is typed into a small window that accepts digits only.
- Anything else is edited as text. The player name is limited to 20
  characters, which is Revival's limit.

The description shown under the list is the comment block that sits above the
key in the ini file, with the comment decoration removed.

While the In-game Netplay mod is hosting, the `Protocol` value on disk is a
temporary one that the mod restores afterwards. The tool does not change it
in that state.

### Hotkeys

A hotkey can be a keyboard key (stored as its `DIK_` name), a controller
button (stored as `BUTTON_1` to `BUTTON_32`), or `None`.

Revival reads button hotkeys from one controller only: the one player 1's A
button is on, or, if that is on the keyboard, the one player 2's A button is
on. If both are on the keyboard Revival reads no controller at all. The
capture window therefore only accepts a button from that controller, and
accepts no controller button when there is none. Stick movement and the D-Pad
are ignored in this window. Esc cancels and Backspace stores `None`.

## The Mods page

### How EfzModManager works

EfzModManager is a `dinput.dll` placed in the game folder. It reads the
`[Mods]` section of `EfzModManager.ini`. For each name set to 1, in the order
they appear in the file, it loads `mods\<Name>\<Name>.dll` if there is one,
and it lets the files under `mods\<Name>\` stand in for the game's own files.
Mods higher in the list win when two mods change the same thing.

So a mod is one folder under `mods\`, and the folder name is also its key in
the ini file. Names cannot contain spaces.

### The list

The list shows, from top to bottom:

1. Folders under `mods\` that the ini file does not mention yet.
2. The entries of `[Mods]`, in file order.
3. Mods from the mod list that are not installed.

Installed mods have a checkbox. An entry of `[Mods]` whose folder does not
exist under `mods\` keeps its place in the list, but it is shown as not
installed and is never checked for updates. Move up and Move down change the order.

When the page is saved, the entry lines of `[Mods]` are rewritten in the new
order. A line that already holds the right mod only gets its value changed,
and only if it was toggled, so comments and untouched lines survive byte for
byte. A folder the ini did not mention is written once it is enabled.

If the game folder has no EfzModManager, selecting the Mods tab offers to
download and install it.

### The mod list

The mod list is a CSV file with the columns Name, Type, Description, Created
By, Download and Folder. Type is Cosmetic, Tool or Gameplay; the type Manager
marks EfzModManager itself. Folder is optional and gives the mod's folder name
when it differs from Name.

The Download column decides what the tool can do with a mod:

- A GitHub repository link: the tool can install and update the mod from that
  repository's latest release.
- Any other link: the tool opens it in the browser.
- No link: the mod is only listed.

A copy of the list is built into the exe. At start the tool downloads the
current list from the project's repository and keeps it in `%APPDATA%`. A
downloaded file is only accepted if it really is a mod list (it has the Name
and Download columns); an error page or a sign-in page never replaces the copy
the tool already has. The downloaded copy is used in preference to the
built-in one.

### Installing and updating

Installing a release means:

1. Find the tag of the repository's latest release.
2. Read the list of files attached to that release.
3. Download its `.zip` file. If the release has no zip, download its `.dll`
   file instead.
4. Import the download (see below).
5. Write `efz_config.release` into the mod's folder with the release tag.

Updates always take the zip as well, because a release may change the mod's
other files and not just its DLL.

Both lookups read ordinary GitHub web pages: the "latest release" link, which
redirects to the tag, and the release's file list, which also shows the
SHA-256 of every file. The tool does not use GitHub's API, because the API
only allows 60 requests an hour without an account, and a few starts with
many mods installed would use that up.

When the tool starts it looks up the latest release tag of every installed
mod that has a repository. A mod needs an update when its recorded tag
differs from the latest tag. The question the tool asks at start, whether to
install the updates now, only covers mods that are enabled. A mod that is
disabled or not listed in `EfzModManager.ini` still shows that an update is
available in the list and can be updated from there.

A mod that was installed by hand has no recorded tag. For such a mod the tool
compares the SHA-256 of the installed DLL with the SHA-256 the release page
shows for the release's `.dll` file. If they are the same, the mod already is
the latest release: the tool records the tag and does not offer an update. If
they differ, or the release has no `.dll` file to compare with, the mod is
offered as an update, and the tool notes which release it compared against
so that it does not fetch the same page again at every start.

The temporary folder a download is unpacked in is deleted file by file. The
Windows shell's own delete function is not used: its first call in a program
can take more than a minute.

On Windows XP the system cannot make the secure connections GitHub requires.
Downloads simply fail there, and mods are installed with Import from files
fetched on another machine.

### Importing

Import takes a `.zip`, a folder or a single `.dll`. Dropping one on the window
does the same.

- A zip is unpacked into a temporary folder first. An entry that tries to
  leave that folder (an absolute path, a drive letter or `..`) makes the whole
  import fail.
- If the unpacked content is one single folder and nothing else, that folder
  is treated as a wrapper and the mod is what is inside it. A single folder
  that belongs to the game's own layout, such as `system`, is not a wrapper:
  it is the mod's content.
- If the content has a `mods` folder or a `dinput.dll` at its top, it is a
  package laid out like the game folder (EfzModManager itself is one) and is
  merged into the game folder.
- Otherwise the content becomes `mods\<Name>\`. When the tool installs a mod
  from the list, the name is the mod's folder from the list. For a manual
  import the name is that of the mod's only DLL if it has exactly one, since
  the loader only loads a DLL named after its folder; failing that it is the
  wrapper folder's name or the file's name.
- Files that already exist are replaced, with one exception: an existing
  `.ini` file is never overwritten. Ini files hold the user's settings.

A newly installed mod is enabled and placed at the top of `[Mods]`.

## Settings pages for mods

The Settings button opens the selected mod's own settings in the same editor
the Revival page uses.

### The exported table

A mod describes its settings by exporting a constant table named
`EFZ_ConfigSchema` from its DLL. The layout is in `include/efz_config_api.h`,
which a mod author copies into the mod.

The table header holds, in order: the schema version (currently 1), the size
of one setting record, the name of the ini file the values live in, the
number of settings, and a pointer to the settings.

Each setting record holds, in order:

| Field | Meaning |
| --- | --- |
| section | The ini section. Settings are grouped by section in the editor. |
| key | The ini key. |
| label | The name shown in the list. When empty, the name is made from the key. |
| description | Shown under the list. A line break in the text starts a new line. May be empty. |
| type | Switch (0 or 1), number, choice, hotkey or text. |
| minimum, maximum | The allowed range of a number. Both zero means any number. |
| choices | For a choice: the options separated by `\|`, at most nine. |
| fallback | The value that is in effect when the key is missing from the ini file. |

All text is UTF-8.

The tool reads this table from the DLL file without running any of the mod's
code. It maps the file the way Windows would, but without resolving its
imports and without calling its entry point. That is why the table must be
constant data: string literals and numbers, nothing that is computed when the
DLL starts. Every pointer in the table is checked to lie inside the mapped
file, and a table that fails the check is ignored. An ini file name that
contains a path is refused, so a mod cannot make the tool write outside its
own folder.

The values are stored in the ini file named by the table, next to the DLL. The
file is created the first time a value is changed. The mod keeps reading its
settings the way it always did; the table only tells the tool what exists.

The record layout is shared with every mod that uses it. Fields may only be
added at the end, together with a higher version number. Existing fields are
never reordered or resized.

### Mods without a table

A mod without the export still gets a Settings page if it has an ini file
named after its folder. The tool then reads the file's comments:

- The comment block directly above a key is its description.
- Lines of the form `; Value - text` or `; Value = text` directly above a key
  are its choices.
- A single line of the form `; 1 = text` makes the key a switch.

Keys that appear before the first section header are not offered.

## Language

The window can be switched between English and Japanese with the button in
the lower left corner. The first time the tool runs it follows the system
language. The Japanese texts of the original program are used where they
exist.

## Version

The version of the tool is shown in blue next to the language button. Clicking
it opens the page of the latest release in the browser. When the tool starts
it looks up the tag of its own latest release, and if that is a higher
version than the running one, "(update!)" is shown after the version. The
tool does not update itself.

## Starting the game on Linux

Under Wine a program gets Wine's own `dinput.dll` and `ddraw.dll` unless Wine
is told otherwise, and then the mod loader and Revival's renderer in the game
folder would never be loaded.

When the tool notices that it is running under Wine it does two things at
start:

- It writes the per-application setting
  `HKCU\Software\Wine\AppDefaults\EFZ.exe\DllOverrides` with `dinput` and
  `ddraw` set to "native, then builtin". This is the setting every Wine
  version honours, and it applies however the game is started afterwards. A
  value the user already set for either DLL is left alone.
- It adds the same override to the `WINEDLLOVERRIDES` environment variable
  for the game it may start. Current Wine versions take this variable from
  the Linux environment and ignore the copy a Windows program sets, so this
  only helps on old versions. It is put in front of whatever the variable
  already holds, so that the user's own entries still win.

It is enough to run the tool once in the Wine prefix the game is played in.

## Building

- The tool is built for Windows XP: the v141_xp toolset, 32 bit, static C
  runtime, and only functions that exist on XP.
- The exe has no application manifest. A manifest switches off the file
  virtualization that Windows applies to old programs installed under
  Program Files. The game has no manifest, so with one the tool would read and
  write a different `KEY.ini` than the game.
- `src/efz_config_resources.rc`, `src/efz_config_tables.h` and
  `src/efz_config_dik.h` are generated. Change `tools/gen_efz_config_ui.py` or
  `tools/gen_efz_config_dik.py` and run them again; do not edit the generated
  files.
- The window layout starts from the original program's dialog. Its OK and
  Cancel buttons have the ids 1000 and 1001, not the standard ones, because
  that is what the original uses. The standard ids only arrive from Enter and
  Esc: Enter edits the selected row of a list and otherwise acts as OK.
- `third_party/miniz` unpacks zip files from memory.
- `ControllsFix` is the mod's own repository, included as a git submodule. The
  tool compiles `padmap.c` from it, and the build also produces the mod's DLL
  and runs its test. The DLL is not part of the tool's releases; the tool
  downloads the mod from the mod's own releases like any other mod.
- The generators read data taken from the original configuration programs
  (their dialogs, texts and key names) from a folder named `extracted`. That
  folder is not part of the repository; the generated files are, so the tool
  builds without it.
- The tests are `test_ini` (the ini line model) and `test_mods` (the mod list
  parser, import rules and reading a settings table from a DLL), plus the mod's
  `test_padmap`. They run with `ctest` and need no network.
