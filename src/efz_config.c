#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0700
#define COBJMACROS

#include <windows.h>
#include <windowsx.h>
#include <initguid.h>
#include <dinput.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "efz_config_resource.h"
#include "efz_config_tables.h"
#include "efz_config_dik.h"
#include "efz_config_ini.h"
#include "efz_config_mods.h"
#include "../include/efz_config_api.h"
#include "../ControllsFix/padmap.h"

#ifndef ARRAYSIZE
#  define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define FORMAT(buffer, ...) \
    (_snwprintf((buffer), ARRAYSIZE(buffer), __VA_ARGS__), (buffer)[ARRAYSIZE(buffer) - 1] = 0)

#pragma pack(push, 1)
typedef struct StoredBinding {
    uint8_t source;
    uint8_t code;
} StoredBinding;

typedef struct KeyPreset {
    StoredBinding bindings[16];
    uint8_t highResolution;
    uint8_t cpuDifficulty;
    uint8_t drawRate;
    uint8_t effects;
    uint8_t damage;
    uint8_t skipOpeningMovie;
    uint8_t roundCount;
} KeyPreset;

typedef struct KeyConfigFile {
    KeyPreset presets[4];
    uint8_t selectedPreset;
} KeyConfigFile;
#pragma pack(pop)

typedef char StaticAssertKeyConfig[(sizeof(KeyConfigFile) == sizeof(kDefaultKeyIni)) ? 1 : -1];

enum {
    CONFIG_PRESET_COUNT = 4,
    BINDINGS_PER_PRESET = 16,
    BINDINGS_PER_PLAYER = 8,
    MAX_JOYSTICKS = 2,
};

enum JoystickStoredCode {
    JOY_CODE_UP = 17,
    JOY_CODE_DOWN = 18,
    JOY_CODE_LEFT = 19,
    JOY_CODE_RIGHT = 20,
};

enum {
    AXIS_CAPTURE_THRESHOLD = 750,
    AXIS_RELEASE_THRESHOLD = 350,
};

static int AxisDirection(const DIJOYSTATE *state, int threshold)
{
    if (state->lX <= -threshold) return JOY_CODE_LEFT;
    if (state->lX >= threshold) return JOY_CODE_RIGHT;
    if (state->lY <= -threshold) return JOY_CODE_UP;
    if (state->lY >= threshold) return JOY_CODE_DOWN;
    return 0;
}

enum { PAGE_COMMON = -1, PAGE_KEYS, PAGE_REVIVAL, PAGE_MODS, PAGE_COUNT };
enum { LANG_EN = 0, LANG_JA = 1 };

static const wchar_t kRegistryKey[] = L"Software\\EfzConfig+";
static const wchar_t kRegistryValue[] = L"Language";

static HINSTANCE g_instance;
static HWND g_mainWindow;
static HWND g_configDialog;
static BOOL g_applicationActive = TRUE;
static int g_lang = LANG_EN;
static int g_activePage = PAGE_KEYS;

static wchar_t g_exeDir[MAX_PATH];
static wchar_t g_keyIniPath[MAX_PATH];
static wchar_t g_gameExePath[MAX_PATH];

static LPDIRECTINPUTA g_directInput;
static LPDIRECTINPUTDEVICEA g_keyboard;
static LPDIRECTINPUTDEVICE2A g_joysticks[MAX_JOYSTICKS];
static unsigned int g_joystickCount;
static char g_joystickNames[MAX_JOYSTICKS][MAX_PATH];
static PadMapConfig g_padMap;
static PadMapDevice g_padDevices[MAX_JOYSTICKS];
static const wchar_t kPadMod[] = L"ControllsFix";

static KeyConfigFile g_keyConfig;
static uint8_t g_currentPreset;

static const int kBindingTextIds[BINDINGS_PER_PRESET] = {
    1005, 1006, 1007, 1008, 1009, 1010, 1011, 1012,
    1013, 1014, 1015, 1016, 1017, 1018, 1019, 1036,
};
static RECT g_gridCells[BINDINGS_PER_PRESET];

typedef struct CaptureEvent {
    int source;
    int code;
    bool pov;
    bool extra;
} CaptureEvent;
static bool g_captureArmed;
static int g_dpadPad[2];
static bool g_prevHadEvent;

static const wchar_t *const kRevivalSections[] = {
    L"Network", L"Practice", L"Tournament", L"Global", L"Others",
};
static IniFile g_revivalIni;
static bool g_revivalAvailable;
static bool g_revivalDirty;
static bool g_protocolOverridden;
static int g_revSection;

static IniFile g_modSettingsIni;
static bool g_modSettings;
static bool g_modSettingsDirty;
static wchar_t g_modSettingsName[MOD_NAME_CAP];
static ModSchema g_modSchema;
static IniFile *g_editIni = &g_revivalIni;
static bool *g_editDirty = &g_revivalDirty;
static wchar_t g_editSection[64];

typedef struct ModRow {
    wchar_t name[MOD_NAME_CAP];
    const ModInfo *info;
    bool installed;
    bool missing;
    bool listed;
    bool enabled;
    bool changed;
    wchar_t installedTag[MOD_TAG_CAP];
} ModRow;
static wchar_t g_modsIniPath[MAX_PATH];
static IniFile g_modsIni;
static bool g_modsAvailable;
static bool g_modsDirty;
static bool g_modsFilling;
static bool g_modBusy;
static ModRow g_modRows[256];
static int g_modRowCount;
static wchar_t g_cataloguePath[MAX_PATH + 64];
static const wchar_t kReleasesUrl[] = L"https://github.com/Aquat1c/EfzConfigPlus/releases/latest";
static const wchar_t kCatalogueUrl[] =
    L"https://raw.githubusercontent.com/Aquat1c/EfzConfigPlus/main/mods_catalogue.csv";

static void ShowPage(HWND dialog, int page);

static const wchar_t *Ls(int stringIndex)
{
    return kLangStrings[stringIndex][g_lang];
}

static int MsgBox(HWND owner, const wchar_t *text, UINT flags)
{
    return MessageBoxW(owner, text, Ls(LS_MSG_CAPTION), flags);
}

static void SetDescription(HWND dialog, int id, const wchar_t *text)
{
    HWND edit = GetDlgItem(dialog, id);
    HDC dc = GetDC(edit);
    HGDIOBJ previous = SelectObject(dc, (HFONT)SendMessageW(edit, WM_GETFONT, 0, 0));
    TEXTMETRICW metrics;
    RECT client;

    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(edit, dc);

    ShowScrollBar(edit, SB_VERT, FALSE);
    SetWindowTextW(edit, text);
    GetClientRect(edit, &client);
    if (SendMessageW(edit, EM_GETLINECOUNT, 0, 0) * metrics.tmHeight > client.bottom)
        ShowScrollBar(edit, SB_VERT, TRUE);
}

static KeyPreset *CurrentPreset(void)
{
    return &g_keyConfig.presets[g_currentPreset];
}

static bool IsDirectionSlot(int slot)
{
    return (slot % BINDINGS_PER_PLAYER) < 4;
}

static bool IsEmptyBinding(const StoredBinding *binding)
{
    return binding->source == 0 && binding->code == 0;
}

static int LoadLanguagePreference(void)
{
    HKEY key;
    wchar_t value[8] = L"";
    DWORD type = 0, size = sizeof(value) - sizeof(wchar_t);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_QUERY_VALUE, &key)
            == ERROR_SUCCESS) {
        LONG rc = RegQueryValueExW(key, kRegistryValue, NULL, &type, (LPBYTE)value, &size);
        RegCloseKey(key);
        if (rc == ERROR_SUCCESS && type == REG_SZ) {
            if (_wcsicmp(value, L"EN") == 0)
                return LANG_EN;
            if (_wcsicmp(value, L"JP") == 0)
                return LANG_JA;
        }
    }
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? LANG_JA : LANG_EN;
}

static void SaveLanguagePreference(int lang)
{
    HKEY key;
    const wchar_t *value = lang == LANG_JA ? L"JP" : L"EN";

    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(key, kRegistryValue, 0, REG_SZ, (const BYTE *)value,
                       (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

static bool SetAxisRange(LPDIRECTINPUTDEVICE2A device, DWORD how, DWORD object)
{
    DIPROPRANGE range;

    memset(&range, 0, sizeof(range));
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = how;
    range.diph.dwObj = object;
    range.lMin = -1000;
    range.lMax = 1000;
    return SUCCEEDED(IDirectInputDevice2_SetProperty(device, DIPROP_RANGE, &range.diph));
}

static BOOL CALLBACK SetAxisRangeCallback(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID context)
{
    SetAxisRange((LPDIRECTINPUTDEVICE2A)context, DIPH_BYID, object->dwType);
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK EnumerateJoystickCallback(LPCDIDEVICEINSTANCEA instance, LPVOID context)
{
    LPDIRECTINPUTDEVICEA baseDevice = NULL;
    HRESULT hr;
    (void)context;

    if (FAILED(IDirectInput_CreateDevice(g_directInput, &instance->guidInstance, &baseDevice, NULL)))
        return DIENUM_CONTINUE;
    hr = IDirectInputDevice_QueryInterface(baseDevice, &IID_IDirectInputDevice2A,
                                           (void **)&g_joysticks[g_joystickCount]);
    IDirectInputDevice_Release(baseDevice);
    if (FAILED(hr))
        return DIENUM_CONTINUE;

    if (FAILED(IDirectInputDevice2_SetDataFormat(g_joysticks[g_joystickCount], &c_dfDIJoystick))
            || !SetAxisRange(g_joysticks[g_joystickCount], DIPH_BYOFFSET, DIJOFS_X)
            || !SetAxisRange(g_joysticks[g_joystickCount], DIPH_BYOFFSET, DIJOFS_Y)) {
        IDirectInputDevice2_Release(g_joysticks[g_joystickCount]);
        g_joysticks[g_joystickCount] = NULL;
        return DIENUM_CONTINUE;
    }

    lstrcpynA(g_joystickNames[g_joystickCount], instance->tszProductName, MAX_PATH);
    ++g_joystickCount;
    return g_joystickCount < MAX_JOYSTICKS ? DIENUM_CONTINUE : DIENUM_STOP;
}

static void ReloadPadMap(void)
{
    wchar_t path[MAX_PATH + 64];

    FORMAT(path, L"%s\\mods\\%s\\%s.ini", g_exeDir, kPadMod, kPadMod);
    PadMap_Load(&g_padMap, path);
}

static void ShutdownDirectInput(void)
{
    unsigned int i;

    if (!g_directInput)
        return;
    if (g_keyboard) {
        IDirectInputDevice_Unacquire(g_keyboard);
        IDirectInputDevice_Release(g_keyboard);
        g_keyboard = NULL;
    }
    for (i = 0; i < MAX_JOYSTICKS; ++i) {
        if (g_joysticks[i]) {
            IDirectInputDevice2_Unacquire(g_joysticks[i]);
            IDirectInputDevice2_Release(g_joysticks[i]);
            g_joysticks[i] = NULL;
        }
    }
    IDirectInput_Release(g_directInput);
    g_directInput = NULL;
    g_joystickCount = 0;
}

static BOOL InitializeDirectInput(HWND cooperativeWindow)
{
    typedef HRESULT (WINAPI *DirectInputCreateAFn)(HINSTANCE, DWORD, LPDIRECTINPUTA *, LPUNKNOWN);
    wchar_t path[MAX_PATH + 16];
    DirectInputCreateAFn create;
    HMODULE module;
    unsigned int i;

    GetSystemDirectoryW(path, MAX_PATH);
    wcscat(path, L"\\dinput.dll");
    module = LoadLibraryW(path);

    create = module ? (DirectInputCreateAFn)GetProcAddress(module, "DirectInputCreateA") : NULL;
    if (!create || FAILED(create(g_instance, DIRECTINPUT_VERSION, &g_directInput, NULL)))
        return FALSE;

    if (FAILED(IDirectInput_CreateDevice(g_directInput, &GUID_SysKeyboard, &g_keyboard, NULL))
        || FAILED(IDirectInputDevice_SetDataFormat(g_keyboard, &c_dfDIKeyboard))
        || FAILED(IDirectInputDevice_SetCooperativeLevel(
            g_keyboard, cooperativeWindow, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) {
        ShutdownDirectInput();
        return FALSE;
    }
    IDirectInputDevice_Acquire(g_keyboard);

    IDirectInput_EnumDevices(g_directInput, DIDEVTYPE_JOYSTICK, EnumerateJoystickCallback,
                             NULL, DIEDFL_ATTACHEDONLY);
    for (i = 0; i < g_joystickCount; ++i) {
        if (FAILED(IDirectInputDevice2_SetCooperativeLevel(
                g_joysticks[i], cooperativeWindow, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)))
            continue;

        IDirectInputDevice2_EnumObjects(g_joysticks[i], SetAxisRangeCallback,
                                        g_joysticks[i], DIDFT_AXIS);
        IDirectInputDevice2_Acquire(g_joysticks[i]);
        PadMap_Describe(g_joysticks[i], &g_padDevices[i]);
    }
    ReloadPadMap();
    return TRUE;
}

static bool ReadKeyboardState(BYTE state[256])
{
    HRESULT hr = IDirectInputDevice_GetDeviceState(g_keyboard, 256, state);
    if (FAILED(hr)) {
        IDirectInputDevice_Acquire(g_keyboard);
        return false;
    }
    return true;
}

static int PollKeyboardCode(bool filterReserved)
{
    BYTE state[256];
    int code;

    if (!ReadKeyboardState(state))
        return -1;
    for (code = 0; code < 256; ++code)
        if (state[code] & 0x80)
            break;
    if (code == 256)
        return -1;

    if (filterReserved
        && (code == DIK_ESCAPE
            || code == DIK_KANJI
            || (code >= DIK_F1 && code <= DIK_F10)
            || (code >= DIK_F11 && code <= DIK_F15)
            || code == DIK_RETURN
            || code == DIK_NUMPADENTER
            || code == DIK_TAB
            || code == DIK_RSHIFT
            || code == DIK_LSHIFT))
        return -1;
    return code;
}

static bool PollJoystickEvent(unsigned int index, int buttonLimit, CaptureEvent *out)
{
    static int s_axisHold[MAX_JOYSTICKS];
    DIJOYSTATE state;
    unsigned extra;
    int i, direction;

    if (index >= MAX_JOYSTICKS || !g_joysticks[index])
        return false;
    if (FAILED(IDirectInputDevice2_Poll(g_joysticks[index]))
        || FAILED(IDirectInputDevice2_GetDeviceState(g_joysticks[index], sizeof(state), &state))) {
        IDirectInputDevice2_Acquire(g_joysticks[index]);
        return false;
    }

    extra = PadMap_Apply(&g_padMap, &g_padDevices[index], &state);
    out->source = (int)index + 1;
    out->pov = false;
    out->extra = false;

    direction = AxisDirection(&state, s_axisHold[index] ? AXIS_RELEASE_THRESHOLD
                                                        : AXIS_CAPTURE_THRESHOLD);
    if (s_axisHold[index] && direction != s_axisHold[index])
        direction = 0;
    s_axisHold[index] = direction;
    if (direction && buttonLimit <= 16) {
        out->code = direction;
        return true;
    }

    for (i = 0; i < buttonLimit && i < 32; ++i) {
        if (state.rgbButtons[i] & 0x80) {
            out->code = i + 1;
            out->extra = i < 16 && (extra >> i & 1);
            return true;
        }
    }
    for (i = 0; i < 4; ++i) {
        if (LOWORD(state.rgdwPOV[i]) != 0xFFFF) {
            out->code = 0;
            out->pov = true;
            return true;
        }
    }
    return false;
}

static bool AnyInputActive(void)
{
    CaptureEvent ev;
    BYTE state[256];
    int i;

    if (PollJoystickEvent(0, 32, &ev) || PollJoystickEvent(1, 32, &ev))
        return true;
    if (!ReadKeyboardState(state))
        return true;
    for (i = 0; i < 256; ++i)
        if (state[i] & 0x80)
            return true;
    return false;
}

static void DisarmCapture(void)
{
    g_captureArmed = false;
    g_prevHadEvent = false;
}

static bool PollCaptureEvent(int buttonLimit, bool filterReservedKeys, CaptureEvent *out)
{
    CaptureEvent ev = { 0, 0, false, false };
    bool have = PollJoystickEvent(0, buttonLimit, &ev) || PollJoystickEvent(1, buttonLimit, &ev);

    if (!have) {
        int code = PollKeyboardCode(filterReservedKeys);
        if (code >= 0) {
            ev.source = 0;
            ev.code = code;
            have = true;
        }
    }

    if (!g_captureArmed) {
        g_captureArmed = !AnyInputActive();
        g_prevHadEvent = have;
        return false;
    }
    if (have && !g_prevHadEvent) {
        g_captureArmed = false;
        g_prevHadEvent = true;
        *out = ev;
        return true;
    }
    g_prevHadEvent = have;
    return false;
}

static bool WriteKeyConfigFile(void)
{
    DWORD written = 0;
    bool ok;
    HANDLE file = CreateFileW(g_keyIniPath, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE)
        return false;
    ok = WriteFile(file, &g_keyConfig, sizeof(g_keyConfig), &written, NULL)
         && written == sizeof(g_keyConfig);
    CloseHandle(file);
    return ok;
}

static bool LoadKeyConfig(void)
{
    DWORD size, transferred = 0;
    bool ok;
    HANDLE file = CreateFileW(g_keyIniPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {
        MsgBox(NULL, Ls(LS_MSG_OPEN_FAILED), MB_OK | MB_ICONERROR);
        return false;
    }
    size = file == INVALID_HANDLE_VALUE ? 0 : GetFileSize(file, NULL);
    if (size != 0) {
        memcpy(&g_keyConfig, kDefaultKeyIni, sizeof(g_keyConfig));
        ok = ReadFile(file, &g_keyConfig, sizeof(g_keyConfig), &transferred, NULL) != 0;
        CloseHandle(file);
        if (!ok) {
            MsgBox(NULL, Ls(LS_MSG_READ_FAILED), MB_OK | MB_ICONERROR);
            return false;
        }
    } else {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        memcpy(&g_keyConfig, kDefaultKeyIni, sizeof(g_keyConfig));
        if (!WriteKeyConfigFile()) {
            MsgBox(NULL, Ls(LS_MSG_OPEN_FAILED), MB_OK | MB_ICONERROR);
            return false;
        }
    }

    if (g_keyConfig.selectedPreset >= CONFIG_PRESET_COUNT)
        g_keyConfig.selectedPreset = 0;
    g_currentPreset = g_keyConfig.selectedPreset;
    return true;
}

static uint8_t ReadRadioGroup(HWND dialog, int firstId, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        if (IsDlgButtonChecked(dialog, firstId + i))
            return (uint8_t)(i + 1);
    return 0;
}

static void RestoreRadioGroup(HWND dialog, uint8_t value, int firstId, int count)
{
    if (value >= 1 && value <= count)
        CheckDlgButton(dialog, firstId + value - 1, BST_CHECKED);
}

static void WarnDPadWithoutPad(HWND dialog)
{
    int player, i;

    for (player = 0; player < 2; ++player) {
        const StoredBinding *bindings = CurrentPreset()->bindings + player * BINDINGS_PER_PLAYER;
        bool dpad = true, padAction = false;
        wchar_t message[512];

        for (i = 0; i < 4; ++i)
            dpad = dpad && IsEmptyBinding(&bindings[i]);
        for (i = 4; i < 8; ++i)
            padAction = padAction || bindings[i].source != 0;
        if (dpad && !padAction) {
            FORMAT(message, Ls(LS_WARN_NO_PAD_ACTION), player + 1);
            MsgBox(dialog, message, MB_OK | MB_ICONINFORMATION);
        }
    }
}

static bool SaveKeyConfig(HWND dialog)
{
    int i;

    for (i = 0; i < CONFIG_PRESET_COUNT; ++i) {
        KeyPreset *preset = &g_keyConfig.presets[i];
        preset->highResolution = IsDlgButtonChecked(dialog, IDC_HIGH_RESOLUTION) != 0;
        preset->cpuDifficulty = ReadRadioGroup(dialog, 1127, 4);
        preset->drawRate = ReadRadioGroup(dialog, 1112, 2);
        preset->effects = ReadRadioGroup(dialog, 1120, 2);
        preset->damage = ReadRadioGroup(dialog, 1124, 3);

        preset->roundCount = ReadRadioGroup(dialog, 1131, 3);
    }
    g_keyConfig.selectedPreset = g_currentPreset;

    if (WriteKeyConfigFile())
        return true;
    MsgBox(dialog, Ls(LS_MSG_WRITE_FAILED), MB_OK | MB_ICONERROR);
    return false;
}

static const wchar_t *BindingName(int slot)
{
    const StoredBinding *bindings = CurrentPreset()->bindings;
    const StoredBinding *binding = &bindings[slot];
    int i;

    if (IsEmptyBinding(binding)) {
        int base = slot - slot % BINDINGS_PER_PLAYER;
        if (!IsDirectionSlot(slot))
            return Ls(LS_NONE_NAME);
        for (i = 0; i < 4; ++i)
            if (!IsEmptyBinding(&bindings[base + i]))
                return Ls(LS_NONE_NAME);
        return Ls(LS_DPAD_NAME);
    }
    if (binding->source == 0)
        return kKeyboardNames[binding->code];
    return kJoystickNames[binding->source == 1 ? 0 : 1][binding->code & 31];
}

static void RefreshPadLabels(HWND dialog);

static void RefreshBindingTexts(HWND dialog)
{
    int slot;
    for (slot = 0; slot < BINDINGS_PER_PRESET; ++slot)
        SetDlgItemTextW(dialog, kBindingTextIds[slot], BindingName(slot));
    RefreshPadLabels(dialog);
}

static int GetSelectedBindingSlot(HWND dialog)
{
    int slot;
    for (slot = 0; slot < BINDINGS_PER_PRESET; ++slot)
        if (IsDlgButtonChecked(dialog, IDC_BINDING_FIRST + slot))
            return slot;
    return -1;
}

static void SelectBindingSlot(HWND dialog, int slot)
{
    int i;
    for (i = 0; i < BINDINGS_PER_PRESET; ++i)
        CheckDlgButton(dialog, IDC_BINDING_FIRST + i, i == slot ? BST_CHECKED : BST_UNCHECKED);
}

static bool PadModEnabled(void);
static bool PadModReady(HWND dialog);

static void PollAndAssignBinding(HWND dialog)
{
    static bool asking;
    CaptureEvent ev;
    KeyPreset *preset = CurrentPreset();
    int slot, i;

    slot = GetSelectedBindingSlot(dialog);
    if (slot < 0) {
        DisarmCapture();
        return;
    }
    if (asking || !PollCaptureEvent(16, true, &ev))
        return;

    if (ev.pov && !IsDirectionSlot(slot))
        return;
    if (ev.extra) {
        bool ready;
        asking = true;
        ready = PadModReady(dialog);
        asking = false;
        DisarmCapture();
        if (!ready)
            return;
    }

    if (ev.source != 0) {
        int base = slot - slot % BINDINGS_PER_PLAYER;
        for (i = base; i < base + BINDINGS_PER_PLAYER; ++i)
            if (preset->bindings[i].source == (ev.source ^ 3))
                break;
        if (i < base + BINDINGS_PER_PLAYER)
            for (i = 0; i < BINDINGS_PER_PRESET; ++i)
                if (preset->bindings[i].source != 0)
                    preset->bindings[i].source ^= 3;
    }

    if (ev.pov) {
        int base = slot - slot % BINDINGS_PER_PLAYER;
        memset(&preset->bindings[base], 0, 4 * sizeof(StoredBinding));
        g_dpadPad[base / BINDINGS_PER_PLAYER] = ev.source;
        slot = base + 4;
    } else {
        for (i = 0; i < BINDINGS_PER_PRESET; ++i)
            if (preset->bindings[i].source == ev.source && preset->bindings[i].code == ev.code)
                memset(&preset->bindings[i], 0, sizeof(StoredBinding));
        preset->bindings[slot].source = (uint8_t)ev.source;
        preset->bindings[slot].code = (uint8_t)ev.code;

        slot = (slot + 1) % BINDINGS_PER_PLAYER ? slot + 1 : -1;
    }

    RefreshBindingTexts(dialog);
    SelectBindingSlot(dialog, slot);
    SetFocus(GetDlgItem(dialog, slot < 0 ? IDC_OK_BUTTON : IDC_CANCEL_BUTTON));
}

static void BuildBindingGrid(HWND dialog)
{
    int slot;

    for (slot = 0; slot < BINDINGS_PER_PRESET; ++slot) {
        RECT radio, text;
        GetWindowRect(GetDlgItem(dialog, IDC_BINDING_FIRST + slot), &radio);
        GetWindowRect(GetDlgItem(dialog, kBindingTextIds[slot]), &text);
        UnionRect(&g_gridCells[slot], &radio, &text);
        MapWindowPoints(NULL, dialog, (LPPOINT)&g_gridCells[slot], 2);
        InflateRect(&g_gridCells[slot], 2, 2);
    }
}

enum { VALUE_CHOICE, VALUE_HOTKEY, VALUE_UINT, VALUE_TEXT };

typedef struct ValueChoices {
    const wchar_t *key;
    const wchar_t *choices[9];
} ValueChoices;

#define MENU_FONTS L"Yu Gothic", L"Meiryo", L"MS Gothic", L"Noto Sans JP", \
                   L"Noto Sans Mono", L"Segoe UI", L"Arial", L"ITC Bolt"
static const ValueChoices kValueChoices[] = {
    { L"Protocol", { L"IPv4", L"IPv6" } },
    { L"OfflineVsHumanMode", { L"VS Human", L"Tournament" } },
    { L"Language", { L"English", L"Japanese" } },
    { L"MenuTtfFont", { MENU_FONTS } },
    { L"HostingTipFont", { MENU_FONTS } },
    { NULL, { L"True", L"False" } },
    { NULL, { L"0", L"1" } },
};

static bool IsUnsignedKey(const wchar_t *key)
{
    return _wcsicmp(key, L"Port") == 0 || _wcsicmp(key, L"MaxRollback") == 0
        || wcsstr(key, L"Window") || wcsstr(key, L"BackBuffer");
}

static bool IsHotkeyValue(const wchar_t *value)
{
    return _wcsnicmp(value, L"DIK_", 4) == 0 || _wcsnicmp(value, L"BUTTON_", 7) == 0
        || _wcsicmp(value, L"None") == 0;
}

static int ClassifyValue(const wchar_t *key, const wchar_t *value, const ValueChoices **choices)
{
    int i, j;

    if (IsUnsignedKey(key))
        return VALUE_UINT;
    for (i = 0; i < (int)ARRAYSIZE(kValueChoices); ++i) {
        const ValueChoices *entry = &kValueChoices[i];
        bool match = entry->key && _wcsicmp(entry->key, key) == 0;
        for (j = 0; !entry->key && !match && j < 9 && entry->choices[j]; ++j)
            match = _wcsicmp(entry->choices[j], value) == 0;
        if (match) {
            *choices = entry;
            return VALUE_CHOICE;
        }
    }
    return IsHotkeyValue(value) ? VALUE_HOTKEY : VALUE_TEXT;
}

static void FriendlyKeyName(const wchar_t *key, wchar_t *out, size_t cap)
{
    size_t n = 0, i;

    for (i = 0; key[i] && n + 2 < cap; ++i) {
        bool upper = key[i] >= L'A' && key[i] <= L'Z';
        bool prevLower = i > 0 && ((key[i - 1] >= L'a' && key[i - 1] <= L'z')
                                   || (key[i - 1] >= L'0' && key[i - 1] <= L'9'));
        if (upper && prevLower)
            out[n++] = L' ';
        out[n++] = key[i] == L'_' ? L' ' : key[i];
    }
    out[n] = 0;
    if (out[0] >= L'a' && out[0] <= L'z')
        out[0] -= L'a' - L'A';
}

static bool SchemaRows(void)
{
    return g_editIni == &g_modSettingsIni && g_modSchema.count > 0;
}

static void SettingValue(const ModSetting *setting, wchar_t *value, size_t cap)
{
    wchar_t key[8];
    int line = Ini_FindKey(g_editIni, setting->section, setting->key);

    if (line < 0 || !Ini_ParseLine(g_editIni, line, key, ARRAYSIZE(key), value, cap)) {
        wcsncpy(value, setting->fallback, cap - 1);
        value[cap - 1] = 0;
    }
}

static void SettingLabel(const ModSetting *setting, wchar_t *label, size_t cap)
{
    if (setting->label[0]) {
        wcsncpy(label, setting->label, cap - 1);
        label[cap - 1] = 0;
    } else {
        FriendlyKeyName(setting->key, label, cap);
    }
}

static int CommentBlock(int line, int *first, bool skipKeys)
{
    wchar_t key[8], value[8];
    int last = line - 1;

    while (skipKeys && last >= 0
            && Ini_ParseLine(g_editIni, last, key, ARRAYSIZE(key), value, ARRAYSIZE(value)))
        --last;
    for (*first = last; *first >= 0; --*first) {
        const wchar_t *text = g_editIni->lines[*first].text;
        while (*text == L' ' || *text == L'\t')
            ++text;
        if (*text != L';')
            break;
    }
    return last;
}

static void DescribeRevivalLine(int line, wchar_t *out, size_t cap)
{
    int first, last = CommentBlock(line, &first, true), i;
    size_t n = 0;

    out[0] = 0;
    for (i = first + 1; i <= last; ++i) {
        const wchar_t *text = g_editIni->lines[i].text, *end;
        while (*text && wcschr(L" \t;/*-", *text))
            ++text;
        end = text + wcslen(text);
        while (end > text && wcschr(L" \t/*-", end[-1]))
            --end;
        if (end == text || n + (size_t)(end - text) + 3 >= cap)
            continue;
        if (n) {
            out[n++] = L'\r';
            out[n++] = L'\n';
        }
        memcpy(out + n, text, (size_t)(end - text) * sizeof(wchar_t));
        n += (size_t)(end - text);
        out[n] = 0;
    }
}

static int CommentChoices(int line, wchar_t choices[9][32])
{
    int first, last = CommentBlock(line, &first, false), count = 0, i;

    for (i = first + 1; i <= last && count < 9; ++i) {
        const wchar_t *text = g_editIni->lines[i].text, *end;
        size_t gap;

        text += wcsspn(text, L" \t;");
        end = text + wcscspn(text, L" \t");
        gap = wcsspn(end, L" \t");
        if (end > text && end - text < 32 && gap && (end[gap] == L'-' || end[gap] == L'=')
                && end[gap + 1] == L' ') {
            memcpy(choices[count], text, (size_t)(end - text) * sizeof(wchar_t));
            choices[count++][end - text] = 0;
        }
    }
    return count;
}

static int SelectedRevivalLine(HWND list, int *itemOut)
{
    LVITEMW item;

    memset(&item, 0, sizeof(item));
    item.iItem = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    item.mask = LVIF_PARAM;
    if (item.iItem < 0 || !SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item))
        return -1;
    if (itemOut)
        *itemOut = item.iItem;
    return (int)item.lParam;
}

static void UpdateRevivalDescription(HWND dialog)
{
    wchar_t text[2048];
    int line = SelectedRevivalLine(GetDlgItem(dialog, IDC_REV_LIST), NULL);

    text[0] = 0;
    if (line >= 0 && SchemaRows()) {
        const wchar_t *in = g_modSchema.settings[line].description;
        size_t n = 0;
        for (; *in && n + 2 < ARRAYSIZE(text); ++in) {
            if (*in == L'\n')
                text[n++] = L'\r';
            if (*in != L'\r')
                text[n++] = *in;
        }
        text[n] = 0;
    }
    else if (line >= 0)
        DescribeRevivalLine(line, text, ARRAYSIZE(text));
    else if (g_editIni == &g_revivalIni && Ini_FindSection(g_editIni, g_editSection) < 0)
        FORMAT(text, Ls(LS_REV_SECTION_MISSING), g_editSection);
    SetDescription(dialog, IDC_REV_DESC, text);
}

static void SetListColumn(HWND list, int index, const wchar_t *text, int width)
{
    LVCOLUMNW column;

    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (LPWSTR)text;
    column.cx = width;
    column.iSubItem = index;
    if (!SendMessageW(list, LVM_SETCOLUMNW, index, (LPARAM)&column))
        SendMessageW(list, LVM_INSERTCOLUMNW, index, (LPARAM)&column);
}

static void SetListItemText(HWND list, int item, int subItem, const wchar_t *text)
{
    LVITEMW lvi;

    memset(&lvi, 0, sizeof(lvi));
    lvi.iSubItem = subItem;
    lvi.pszText = (LPWSTR)text;
    SendMessageW(list, LVM_SETITEMTEXTW, item, (LPARAM)&lvi);
}

static int AddListItem(HWND list, const wchar_t *text, LPARAM param)
{
    LVITEMW lvi;

    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    lvi.iItem = ListView_GetItemCount(list);
    lvi.pszText = (LPWSTR)text;
    lvi.lParam = param;
    return (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
}

static void FillRevivalList(HWND dialog, int sectionIndex)
{
    HWND list = GetDlgItem(dialog, IDC_REV_LIST);
    wchar_t key[128], label[160], value[512];
    int section, i;

    g_editSection[0] = 0;
    if (SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_GETTEXTLEN, sectionIndex, 0)
            < (LRESULT)ARRAYSIZE(g_editSection))
        SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_GETTEXT, sectionIndex, (LPARAM)g_editSection);
    section = Ini_FindSection(g_editIni, g_editSection);
    if (g_editIni == &g_revivalIni)
        g_revSection = sectionIndex;

    ListView_DeleteAllItems(list);
    for (i = 0; SchemaRows() && i < g_modSchema.count; ++i) {
        const ModSetting *setting = &g_modSchema.settings[i];
        if (_wcsicmp(setting->section, g_editSection) != 0)
            continue;
        SettingLabel(setting, label, ARRAYSIZE(label));
        SettingValue(setting, value, ARRAYSIZE(value));
        SetListItemText(list, AddListItem(list, label, i), 1, value);
    }
    for (i = SchemaRows() || section < 0 ? g_editIni->line_count
                                         : g_editIni->sections[section].first_line + 1;
            i < g_editIni->line_count && g_editIni->lines[i].section == section; ++i) {
        if (!Ini_ParseLine(g_editIni, i, key, ARRAYSIZE(key), value, ARRAYSIZE(value)))
            continue;
        FriendlyKeyName(key, label, ARRAYSIZE(label));
        SetListItemText(list, AddListItem(list, label, i), 1, value);
    }
    ListView_SetColumnWidth(list, 1, LVSCW_AUTOSIZE_USEHEADER);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    UpdateRevivalDescription(dialog);
}

static void BindEditor(HWND dialog, bool mod)
{
    wchar_t hint[160];
    int select = mod ? 0 : g_revSection, i;

    g_editIni = mod ? &g_modSettingsIni : &g_revivalIni;
    g_editDirty = mod ? &g_modSettingsDirty : &g_revivalDirty;
    SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_RESETCONTENT, 0, 0);
    for (i = 0; mod && i < g_modSchema.count; ++i)
        if (SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_FINDSTRINGEXACT, (WPARAM)-1,
                                (LPARAM)g_modSchema.settings[i].section) == LB_ERR)
            SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_ADDSTRING, 0,
                                (LPARAM)g_modSchema.settings[i].section);
    for (i = 0; mod && !g_modSchema.count && i < g_modSettingsIni.section_count; ++i)
        SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_ADDSTRING, 0,
                            (LPARAM)g_modSettingsIni.sections[i].name);
    for (i = 0; !mod && i < (int)ARRAYSIZE(kRevivalSections); ++i)
        SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_ADDSTRING, 0, (LPARAM)kRevivalSections[i]);
    SendDlgItemMessageW(dialog, IDC_REV_SECTIONS, LB_SETCURSEL, select, 0);

    FORMAT(hint, Ls(mod ? LS_MODSET_HINT : LS_REV_HINT_EDIT), g_modSettingsName);
    SetDlgItemTextW(dialog, IDC_REV_HINT, hint);
    FillRevivalList(dialog, select);
}

typedef struct CapturePopup {
    const wchar_t *label;
    wchar_t result[64];
} CapturePopup;

static INT_PTR CALLBACK CapturePopupProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    static CapturePopup *ctx;
    wchar_t prompt[512];
    CaptureEvent ev;

    switch (message) {
    case WM_INITDIALOG:
        ctx = (CapturePopup *)lParam;
        SetWindowTextW(dialog, Ls(LS_CAPTURE_TITLE));
        FORMAT(prompt, Ls(LS_CAPTURE_PROMPT), ctx->label);
        SetDlgItemTextW(dialog, IDC_CAPTURE_PROMPT, prompt);
        DisarmCapture();
        SetTimer(dialog, 1, 10, NULL);
        return TRUE;

    case WM_TIMER:
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            EndDialog(dialog, 0);
        } else if (GetAsyncKeyState(VK_BACK) & 0x8000) {
            wcscpy(ctx->result, L"None");
            EndDialog(dialog, 1);
        } else if (PollCaptureEvent(32, false, &ev) && !ev.pov && (!ev.extra || PadModEnabled())) {
            const StoredBinding *bindings = CurrentPreset()->bindings;
            int pad = bindings[4].source ? bindings[4].source
                                         : bindings[BINDINGS_PER_PLAYER + 4].source;
            if (ev.source == 0 && kDikNames[ev.code]) {
                FORMAT(ctx->result, L"%hs", kDikNames[ev.code]);
                EndDialog(dialog, 1);
            } else if (ev.source != 0 && ev.source == pad && ev.code <= 32) {
                FORMAT(ctx->result, L"BUTTON_%d", ev.code);
                EndDialog(dialog, 1);
            }
        }
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL)
            EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

typedef struct ValuePopup {
    const wchar_t *key, *label;
    wchar_t *value;
    size_t cap;
    bool numeric;
    int minimum, maximum;
} ValuePopup;

static bool ValueAccepted(const ValuePopup *ctx)
{
    const wchar_t *v = ctx->value;
    size_t len = wcslen(v);

    if (ctx->numeric && (len == 0 || wcsspn(v, L"0123456789") != len))
        return false;
    if (ctx->maximum > ctx->minimum) {
        wchar_t *end;
        long number = wcstol(v, &end, 10);
        return len && !*end && number >= ctx->minimum && number <= ctx->maximum;
    }
    if (g_editIni != &g_revivalIni)
        return true;
    if (_wcsicmp(ctx->key, L"Port") == 0)
        return _wtoi(v) >= 1 && _wtoi(v) <= 65535;
    if (_wcsicmp(ctx->key, L"Name") == 0)
        return len >= 1 && len <= 20;
    return true;
}

static INT_PTR CALLBACK ValuePopupProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    static ValuePopup *ctx;
    HWND edit = GetDlgItem(dialog, IDC_VALUE_EDIT_BOX);

    switch (message) {
    case WM_INITDIALOG:
        ctx = (ValuePopup *)lParam;
        SetWindowTextW(dialog, Ls(LS_EDIT_TITLE));
        SetDlgItemTextW(dialog, IDC_VALUE_EDIT_LABEL, ctx->label);
        SetDlgItemTextW(dialog, IDOK, kLangControlTexts[0].text[g_lang]);
        SetDlgItemTextW(dialog, IDCANCEL, kLangControlTexts[1].text[g_lang]);
        if (ctx->numeric)
            SetWindowLongW(edit, GWL_STYLE, GetWindowLongW(edit, GWL_STYLE) | ES_NUMBER);
        SendMessageW(edit, EM_LIMITTEXT, ctx->numeric ? 10 : ctx->cap - 1, 0);
        SetWindowTextW(edit, ctx->value);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t previous[512];
            wcsncpy(previous, ctx->value, ARRAYSIZE(previous) - 1);
            previous[ARRAYSIZE(previous) - 1] = 0;
            GetWindowTextW(edit, ctx->value, (int)ctx->cap);
            if (ValueAccepted(ctx)) {
                EndDialog(dialog, 1);
            } else {
                wcscpy(ctx->value, previous);
                MessageBeep(MB_ICONWARNING);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                SetFocus(edit);
            }
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, 0);
        }
        return TRUE;
    }
    return FALSE;
}

static void EditRevivalRow(HWND dialog)
{
    HWND list = GetDlgItem(dialog, IDC_REV_LIST);
    bool revival = g_editIni == &g_revivalIni;
    const ValueChoices *known = NULL;
    const wchar_t *cycle[9];
    wchar_t found[9][32], key[128], label[160], value[512], newValue[512];
    int item, i, count = 0, kind;
    int line = SelectedRevivalLine(list, &item);
    const ModSetting *setting = SchemaRows() && line >= 0 ? &g_modSchema.settings[line] : NULL;
    const wchar_t *section = setting ? setting->section : g_editSection;

    if (setting) {
        wcscpy(key, setting->key);
        SettingLabel(setting, label, ARRAYSIZE(label));
        SettingValue(setting, value, ARRAYSIZE(value));
    } else if (line < 0
               || !Ini_ParseLine(g_editIni, line, key, ARRAYSIZE(key), value, ARRAYSIZE(value))) {
        return;
    } else {
        FriendlyKeyName(key, label, ARRAYSIZE(label));
    }
    wcscpy(newValue, value);

    if (setting) {
        static const int kKinds[] = { VALUE_CHOICE, VALUE_UINT, VALUE_CHOICE, VALUE_HOTKEY, VALUE_TEXT };
        kind = kKinds[setting->type];
        for (; setting->type == EFZ_SETTING_CHOICE && count < setting->choiceCount; ++count)
            cycle[count] = setting->choices[count];
        if (setting->type == EFZ_SETTING_SWITCH) {
            cycle[0] = L"0";
            cycle[1] = L"1";
            count = 2;
        }
        if (kind == VALUE_CHOICE && count == 0)
            kind = VALUE_TEXT;
    } else if (revival) {
        kind = ClassifyValue(key, value, &known);
        for (; known && count < 9 && known->choices[count]; ++count)
            cycle[count] = known->choices[count];
    } else {
        count = CommentChoices(line, found);
        for (i = 0; i < count; ++i)
            cycle[i] = found[i];
        if (count == 1 && wcscmp(found[0], L"1") == 0) {
            cycle[0] = L"0";
            cycle[1] = L"1";
            count = 2;
        }
        for (i = 0; i < count && _wcsicmp(cycle[i], value) != 0; ++i)
            ;
        kind = count >= 2 && i < count ? VALUE_CHOICE : IsHotkeyValue(value) ? VALUE_HOTKEY : VALUE_TEXT;
    }

    switch (kind) {
    case VALUE_CHOICE:

        if (revival && g_protocolOverridden && _wcsicmp(key, L"Protocol") == 0) {
            MsgBox(dialog, Ls(LS_REV_READONLY), MB_OK | MB_ICONINFORMATION);
            return;
        }
        for (i = 0; i < count && _wcsicmp(cycle[i], value) != 0; ++i)
            ;
        wcscpy(newValue, cycle[i + 1 < count ? i + 1 : 0]);
        break;

    case VALUE_HOTKEY: {
        CapturePopup popup;
        popup.label = label;
        if (DialogBoxParamW(g_instance, MAKEINTRESOURCEW(IDD_CAPTURE), dialog,
                            CapturePopupProc, (LPARAM)&popup) != 1)
            return;
        wcscpy(newValue, popup.result);
        break;
    }

    default: {
        ValuePopup popup;
        popup.key = key;
        popup.label = label;
        popup.value = newValue;
        popup.cap = 256;
        popup.minimum = setting ? setting->minimum : 0;
        popup.maximum = setting ? setting->maximum : 0;
        popup.numeric = setting ? kind == VALUE_UINT && setting->minimum >= 0
                      : revival ? IsUnsignedKey(key)
                                : value[0] && wcsspn(value, L"0123456789") == wcslen(value);
        if (DialogBoxParamW(g_instance, MAKEINTRESOURCEW(IDD_VALUE_EDIT), dialog,
                            ValuePopupProc, (LPARAM)&popup) != 1)
            return;
        break;
    }
    }

    if (wcscmp(value, newValue) != 0
            && (Ini_FindKey(g_editIni, section, key) >= 0
                    ? Ini_SetValue(g_editIni, section, key, newValue)
                    : setting && Ini_InsertKey(g_editIni, section, key, newValue, false))) {
        SetListItemText(list, item, 1, newValue);
        *g_editDirty = true;
    }
}

static const wchar_t *LatestTag(const ModInfo *info)
{
    return info ? info->latest : L"";
}

static ModInfo *CatalogueEntry(const wchar_t *name, bool byFolder)
{
    int i;

    for (i = 0; i < g_modCatalogueCount; ++i)
        if (g_modCatalogue[i].type != MOD_MANAGER
                && _wcsicmp(byFolder ? g_modCatalogue[i].folder : g_modCatalogue[i].name, name) == 0)
            return &g_modCatalogue[i];
    return Mods_Bundled(name);
}

static const ModInfo *ManagerPackage(void)
{
    int i;

    for (i = 0; i < g_modCatalogueCount; ++i)
        if (g_modCatalogue[i].type == MOD_MANAGER)
            return &g_modCatalogue[i];
    return NULL;
}

static bool ModFilePath(const wchar_t *folder, const wchar_t *file, wchar_t path[MAX_PATH * 2])
{
    _snwprintf(path, MAX_PATH * 2, L"%s\\mods\\%s\\%s", g_exeDir, folder, file);
    path[MAX_PATH * 2 - 1] = 0;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static bool LoadModSettings(const wchar_t *folder, ModSchema *schema, IniFile *ini)
{
    wchar_t file[MOD_NAME_CAP + 8], path[MAX_PATH * 2];
    bool loaded;

    Mods_FreeSchema(schema);
    Ini_Free(ini);
    FORMAT(file, L"%s.dll", folder);
    if (ModFilePath(folder, file, path) && Mods_LoadSchema(path, schema))
        FORMAT(file, L"%s", schema->iniFile);
    else
        FORMAT(file, L"%s.ini", folder);
    ModFilePath(folder, file, path);
    loaded = wcslen(path) < MAX_PATH && Ini_Load(ini, path);
    if (!loaded && schema->count && wcslen(path) < MAX_PATH) {
        ini->encoding = INI_ENC_ANSI;
        ini->crlf = true;
        return true;
    }
    return loaded && (schema->count || ini->section_count);
}

static bool ModNeedsUpdate(const ModRow *row)
{
    const wchar_t *latest = LatestTag(row->info);
    return row->installed && !row->missing && latest[0] && wcscmp(latest, row->installedTag) != 0;
}

static ModRow *AddModRow(const wchar_t *name)
{
    ModRow *row;
    int i;

    for (i = 0; i < g_modRowCount; ++i)
        if (_wcsicmp(g_modRows[i].name, name) == 0)
            return &g_modRows[i];
    if (g_modRowCount == (int)ARRAYSIZE(g_modRows) || wcslen(name) >= MOD_NAME_CAP)
        return NULL;
    row = &g_modRows[g_modRowCount++];
    memset(row, 0, sizeof(*row));
    wcscpy(row->name, name);
    row->info = CatalogueEntry(name, true);
    return row;
}

static void BuildModRows(void)
{
    int section = Ini_FindSection(&g_modsIni, L"Mods");
    wchar_t key[128], value[16], pattern[MAX_PATH], recorded[MOD_NAME_CAP];
    WIN32_FIND_DATAW found;
    const ModInfo *info;
    HANDLE find;
    ModRow *row;
    int i, r;

    g_modRowCount = 0;
    FORMAT(pattern, L"%s\\mods\\*", g_exeDir);
    find = FindFirstFileW(pattern, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && found.cFileName[0] != L'.'
                    && Ini_FindKey(&g_modsIni, L"Mods", found.cFileName) < 0
                    && (row = AddModRow(found.cFileName)) != NULL)
                row->installed = true;
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }

    for (i = section < 0 ? g_modsIni.line_count : g_modsIni.sections[section].first_line + 1;
            i < g_modsIni.line_count && g_modsIni.lines[i].section == section; ++i) {
        if (Ini_ParseLine(&g_modsIni, i, key, ARRAYSIZE(key), value, ARRAYSIZE(value))
                && (row = AddModRow(key)) != NULL && !row->listed) {
            row->installed = row->listed = true;
            row->enabled = _wtoi(value) != 0;
            FORMAT(pattern, L"%s\\mods\\%s", g_exeDir, key);
            row->missing = GetFileAttributesW(pattern) == INVALID_FILE_ATTRIBUTES;
        }
    }

    for (i = 0; i < g_modRowCount; ++i) {
        Mods_InstalledRelease(g_exeDir, g_modRows[i].name, g_modRows[i].installedTag, recorded);
        if (recorded[0] && (info = CatalogueEntry(recorded, false)) != NULL)
            g_modRows[i].info = info;
    }
    for (i = 0; i < g_modCatalogueCount; ++i) {
        info = &g_modCatalogue[i];
        for (r = 0; r < g_modRowCount && g_modRows[r].info != info; ++r)
            ;
        if (r == g_modRowCount && info->type != MOD_MANAGER)
            AddModRow(info->folder);
    }
}

static int SelectedModRow(HWND dialog)
{
    return ListView_GetNextItem(GetDlgItem(dialog, IDC_MODS_LIST), -1, LVNI_SELECTED);
}

static void UpdateModsSelection(HWND dialog)
{
    int index = SelectedModRow(dialog), i;
    const ModRow *row = index >= 0 ? &g_modRows[index] : NULL;
    const ModInfo *info = row ? row->info : NULL;
    wchar_t text[1024] = L"", author[128];
    bool anyUpdate = false, hasSettings = false;
    int label = LS_BTN_INSTALL;

    if (info) {
        FORMAT(author, Ls(LS_MODS_BY), info->author);
        FORMAT(text, L"%s\r\n%s%s%s", info->description, author,
               info->type == MOD_GAMEPLAY ? L"\r\n" : L"",
               info->type == MOD_GAMEPLAY ? Ls(LS_MODS_GAMEPLAY_WARNING) : L"");
        label = row->installed ? LS_BTN_UPDATE : info->repo[0] ? LS_BTN_INSTALL : LS_BTN_PAGE;
    }
    SetDescription(dialog, IDC_MODS_DESC, text);
    SetDlgItemTextW(dialog, IDC_MODS_INSTALL, Ls(label));

    for (i = 0; i < g_modRowCount; ++i)
        anyUpdate = anyUpdate || ModNeedsUpdate(&g_modRows[i]);
    EnableWindow(GetDlgItem(dialog, IDC_MODS_UP),
                 row && row->installed && index > 0);
    EnableWindow(GetDlgItem(dialog, IDC_MODS_DOWN),
                 row && row->installed && index + 1 < g_modRowCount && row[1].installed);
    EnableWindow(GetDlgItem(dialog, IDC_MODS_INSTALL),
                 info && !g_modBusy && (info->repo[0] || (info->page[0] && !row->installed)));
    if (row && row->installed && !g_modSettings) {
        ModSchema schema = { L"", 0, NULL };
        IniFile ini;
        memset(&ini, 0, sizeof(ini));
        hasSettings = LoadModSettings(row->name, &schema, &ini);
        Mods_FreeSchema(&schema);
        Ini_Free(&ini);
    }
    EnableWindow(GetDlgItem(dialog, IDC_MODS_SETTINGS), hasSettings);
    EnableWindow(GetDlgItem(dialog, IDC_MODS_UPDATE_ALL), anyUpdate && !g_modBusy);
    EnableWindow(GetDlgItem(dialog, IDC_MODS_IMPORT), !g_modBusy);
}

static void FillModsList(HWND dialog, int select)
{
    HWND list = GetDlgItem(dialog, IDC_MODS_LIST);
    wchar_t status[64];
    int i;

    g_modsFilling = true;
    ListView_DeleteAllItems(list);
    for (i = 0; i < g_modRowCount; ++i) {
        const ModRow *row = &g_modRows[i];
        const wchar_t *latest = LatestTag(row->info);

        if (!row->installed || row->missing)
            FORMAT(status, L"%s", Ls(LS_ST_NOT_INSTALLED));
        else if (ModNeedsUpdate(row))
            FORMAT(status, Ls(LS_ST_UPDATE), latest);
        else
            FORMAT(status, L"%s", latest[0] ? Ls(LS_ST_CURRENT) : row->installedTag);

        AddListItem(list, row->info ? row->info->name : row->name, i);
        SetListItemText(list, i, 1, row->info ? Ls(LS_TYPE_COSMETIC + row->info->type) : L"");
        SetListItemText(list, i, 2, status);

        ListView_SetItemState(list, i, INDEXTOSTATEIMAGEMASK(!row->installed ? 0 : row->enabled ? 2 : 1),
                              LVIS_STATEIMAGEMASK);
    }
    g_modsFilling = false;
    ListView_SetColumnWidth(list, 2, LVSCW_AUTOSIZE_USEHEADER);
    if (select >= 0 && select < g_modRowCount) {
        ListView_SetItemState(list, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, select, FALSE);
    }
    UpdateModsSelection(dialog);
}

static bool SaveMods(void)
{
    wchar_t key[128], value[16], text[MOD_NAME_CAP + 4];
    int rows = 0, lines = 0, section, i, r = 0;

    for (i = 0; i < g_modRowCount; ++i)
        rows += g_modRows[i].listed || g_modRows[i].enabled;
    for (;;) {
        section = Ini_FindSection(&g_modsIni, L"Mods");
        lines = 0;
        for (i = section < 0 ? g_modsIni.line_count : g_modsIni.sections[section].first_line + 1;
                i < g_modsIni.line_count && g_modsIni.lines[i].section == section; ++i)
            lines += Ini_ParseLine(&g_modsIni, i, key, ARRAYSIZE(key), value, ARRAYSIZE(value));
        if (lines >= rows)
            break;
        if (!Ini_InsertKey(&g_modsIni, L"Mods", L"-", L"0", true))
            return false;
    }

    for (i = section < 0 ? g_modsIni.line_count : g_modsIni.sections[section].first_line + 1;
            i < g_modsIni.line_count && g_modsIni.lines[i].section == section; ++i) {
        ModRow *row;

        if (!Ini_ParseLine(&g_modsIni, i, key, ARRAYSIZE(key), value, ARRAYSIZE(value)))
            continue;
        while (r < g_modRowCount && !(g_modRows[r].listed || g_modRows[r].enabled))
            ++r;
        if (r == g_modRowCount)
            break;
        row = &g_modRows[r++];
        FORMAT(text, L"%s=%d", row->name, row->enabled);
        if (_wcsicmp(key, row->name) != 0) {
            if (!Ini_ReplaceLine(&g_modsIni, i, text))
                return false;
        } else if (row->changed) {
            Ini_SetValue(&g_modsIni, L"Mods", row->name, row->enabled ? L"1" : L"0");
        }
        row->listed = true;
        row->changed = false;
    }
    return Ini_Save(&g_modsIni);
}

static void ReloadMods(HWND dialog)
{
    int selected = SelectedModRow(dialog);

    Ini_Free(&g_modsIni);
    g_modsAvailable = Ini_Load(&g_modsIni, g_modsIniPath);
    g_modsDirty = false;
    BuildModRows();
    FillModsList(dialog, selected < 0 ? 0 : selected);
}

static bool FlushMods(HWND dialog)
{
    if (g_modsDirty && !SaveMods()) {
        MsgBox(dialog, Ls(LS_MODS_SAVE_FAILED), MB_OK | MB_ICONERROR);
        return false;
    }
    g_modsDirty = false;
    return true;
}

static void ListInstalledMod(const wchar_t *name)
{
    if (name[0] && Ini_FindKey(&g_modsIni, L"Mods", name) < 0)
        Ini_InsertKey(&g_modsIni, L"Mods", name, L"1", true);
}

static void SetModsStatus(HWND dialog, const wchar_t *text)
{
    SetDlgItemTextW(dialog, IDC_MODS_HINT, text ? text : Ls(LS_MODS_HINT));
}

#define WM_APP_MODS_PROGRESS (WM_APP + 1)
#define WM_APP_MODS_DONE     (WM_APP + 2)
#define WM_APP_MODS_CHECK    (WM_APP + 3)

enum { TASK_CATALOGUE, TASK_CHECK, TASK_INSTALL };

typedef struct ModTask {
    HWND dialog;
    int kind;
    bool automatic;
    int count;
    ModInfo mods[64];
    wchar_t folders[64][MOD_NAME_CAP];
    bool ok[64];
} ModTask;

static const wchar_t kOwnRepo[] = L"Aquat1c/EfzConfigPlus";
static bool g_newerRelease;

static bool TagIsNewer(const wchar_t *tag, const wchar_t *version)
{
    int part;

    for (part = 0; part < 4; ++part) {
        long a, b;
        while (*tag && !iswdigit(*tag)) ++tag;
        while (*version && !iswdigit(*version)) ++version;
        a = wcstol(tag, (wchar_t **)&tag, 10);
        b = wcstol(version, (wchar_t **)&version, 10);
        if (a != b)
            return a > b;
    }
    return false;
}

static DWORD WINAPI ModTaskThread(LPVOID parameter)
{
    ModTask *task = (ModTask *)parameter;
    int i;

    for (i = 0; i < task->count; ++i) {
        PostMessageW(task->dialog, WM_APP_MODS_PROGRESS, i, (LPARAM)task);
        if (task->kind == TASK_CATALOGUE) {
            ModInfo self;
            memset(&self, 0, sizeof(self));
            wcscpy(self.repo, kOwnRepo);
            task->ok[i] = Mods_FetchCatalogue(kCatalogueUrl, g_cataloguePath);
            if (Mods_CheckLatest(&self))
                g_newerRelease = TagIsNewer(self.latest, EFZ_CONFIG_VERSION);
        } else if (task->kind == TASK_CHECK) {
            task->ok[i] = Mods_CheckLatest(&task->mods[i]);
            if (task->ok[i])
                Mods_AdoptInstalled(g_exeDir, &task->mods[i]);
        } else {
            task->ok[i] = Mods_InstallRelease(g_exeDir, &task->mods[i], task->folders[i]);
        }
    }
    PostMessageW(task->dialog, WM_APP_MODS_DONE, 0, (LPARAM)task);
    return 0;
}

static void StartModTask(HWND dialog, ModTask *task)
{
    HANDLE thread;

    task->dialog = dialog;
    thread = task->count ? CreateThread(NULL, 0, ModTaskThread, task, 0, NULL) : NULL;
    if (!thread) {
        free(task);
        return;
    }
    CloseHandle(thread);
    g_modBusy = true;
    UpdateModsSelection(dialog);
}

static void FetchCatalogue(HWND dialog)
{
    ModTask *task = (ModTask *)calloc(1, sizeof(ModTask));

    if (!task || g_modBusy) {
        free(task);
        return;
    }
    task->kind = TASK_CATALOGUE;
    task->count = 1;
    StartModTask(dialog, task);
}

static void CheckForUpdates(HWND dialog, bool automatic)
{
    ModTask *task = (ModTask *)calloc(1, sizeof(ModTask));
    int i;

    if (!task || g_modBusy) {
        free(task);
        return;
    }
    task->kind = TASK_CHECK;
    task->automatic = automatic;
    for (i = 0; i < g_modRowCount && task->count < 64; ++i)
        if (g_modRows[i].installed && !g_modRows[i].missing && g_modRows[i].info && g_modRows[i].info->repo[0])
            task->mods[task->count++] = *g_modRows[i].info;
    StartModTask(dialog, task);
}

static void InstallReleases(HWND dialog, const ModInfo *only, bool updates, bool enabledOnly)
{
    ModTask *task = (ModTask *)calloc(1, sizeof(ModTask));
    int i;

    if (!task || g_modBusy || !FlushMods(dialog)) {
        free(task);
        return;
    }
    task->kind = TASK_INSTALL;
    if (only)
        task->mods[task->count++] = *only;
    for (i = 0; updates && i < g_modRowCount && task->count < 64; ++i)
        if (ModNeedsUpdate(&g_modRows[i]) && (g_modRows[i].enabled || !enabledOnly))
            task->mods[task->count++] = *g_modRows[i].info;
    StartModTask(dialog, task);
}

static int PadModRow(void)
{
    int i;

    for (i = 0; i < g_modRowCount; ++i)
        if (_wcsicmp(g_modRows[i].name, kPadMod) == 0 && g_modRows[i].installed && !g_modRows[i].missing)
            return i;
    return -1;
}

static bool PadModEnabled(void)
{
    int row = PadModRow();
    return row >= 0 && g_modRows[row].enabled;
}

static bool PadModReady(HWND dialog)
{
    wchar_t message[512];
    ModInfo *info;
    int row;

    if (PadModEnabled())
        return true;
    if (!g_modsAvailable) {
        MsgBox(dialog, Ls(LS_PADMOD_NEEDS_MANAGER), MB_OK | MB_ICONINFORMATION);
        return false;
    }
    if (MsgBox(dialog, Ls(LS_PADMOD_PROMPT), MB_YESNO | MB_ICONQUESTION) != IDYES)
        return false;

    row = PadModRow();
    if (row >= 0) {
        g_modRows[row].enabled = true;
        g_modRows[row].changed = true;
        g_modsDirty = true;
        FillModsList(dialog, SelectedModRow(dialog));
        return true;
    }
    info = CatalogueEntry(kPadMod, false);
    if (!info || !info->repo[0] || g_modBusy) {
        FORMAT(message, Ls(LS_MODS_INSTALL_FAILED), kPadMod);
        MsgBox(dialog, message, MB_OK | MB_ICONWARNING);
        return false;
    }
    InstallReleases(dialog, info, false, false);
    return true;
}

static void OnModTaskDone(HWND dialog, ModTask *task)
{
    wchar_t message[1024] = L"", names[512] = L"";
    bool prompt = false;
    int i;

    g_modBusy = false;
    SetModsStatus(dialog, NULL);

    if (task->kind == TASK_CATALOGUE) {
        if (task->ok[0] && !g_modsDirty && Mods_LoadCatalogueFile(g_cataloguePath)) {
            BuildModRows();
            FillModsList(dialog, SelectedModRow(dialog));
        }
        free(task);
        UpdateModsSelection(dialog);
        if (g_modsAvailable)
            CheckForUpdates(dialog, true);
        return;
    }

    for (i = 0; i < task->count; ++i) {
        ModInfo *entry = CatalogueEntry(task->mods[i].name, false);
        if (task->ok[i] && entry)
            wcscpy(entry->latest, task->mods[i].latest);
        if (task->kind != TASK_INSTALL)
            continue;
        if (!task->ok[i]) {
            FORMAT(message, Ls(LS_MODS_INSTALL_FAILED), task->mods[i].name);
            MsgBox(dialog, message, MB_OK | MB_ICONWARNING);
        } else {
            ListInstalledMod(task->folders[i]);
        }
    }

    if (task->kind == TASK_INSTALL) {
        bool hadManager = g_modsAvailable;
        if (g_modsAvailable && !Ini_Save(&g_modsIni))
            MsgBox(dialog, Ls(LS_MODS_SAVE_FAILED), MB_OK | MB_ICONERROR);
        ReloadMods(dialog);
        if (!hadManager && g_modsAvailable) {
            SendDlgItemMessageW(dialog, IDC_MAIN_TAB, TCM_SETCURSEL, PAGE_MODS, 0);
            ShowPage(dialog, PAGE_MODS);
            CheckForUpdates(dialog, false);
        }
    } else {
        for (i = 0; i < g_modRowCount; ++i)
            Mods_InstalledRelease(g_exeDir, g_modRows[i].name, g_modRows[i].installedTag, message);
        message[0] = 0;
        FillModsList(dialog, SelectedModRow(dialog));
        for (i = 0; i < g_modRowCount; ++i) {
            if (ModNeedsUpdate(&g_modRows[i]) && g_modRows[i].enabled && wcslen(names) + MOD_NAME_CAP + 48 < ARRAYSIZE(names)) {
                wcscat(names, g_modRows[i].info->name);
                wcscat(names, L"  (");
                wcscat(names, LatestTag(g_modRows[i].info));
                wcscat(names, L")\n");
                prompt = true;
            }
        }
        if (prompt && task->automatic) {
            FORMAT(message, Ls(LS_MODS_UPDATES_PROMPT), names);
            if (MsgBox(dialog, message, MB_YESNO | MB_ICONQUESTION) == IDYES)
                InstallReleases(dialog, NULL, true, true);
        }
    }
    free(task);
}

static bool SaveModSettings(HWND dialog)
{
    wchar_t message[256];

    if (g_modSettingsDirty && Ini_Save(&g_modSettingsIni)) {
        g_modSettingsDirty = false;
        ReloadPadMap();
    }
    if (g_modSettingsDirty) {
        FORMAT(message, Ls(LS_MODSET_SAVE_FAILED), g_modSettingsName);
        MsgBox(dialog, message, MB_OK | MB_ICONERROR);
        return false;
    }
    g_modSettingsDirty = false;
    return true;
}

static void ShowModSettings(HWND dialog, bool show)
{
    int index = SelectedModRow(dialog);
    const ModRow *row = index >= 0 ? &g_modRows[index] : NULL;

    if (!SaveModSettings(dialog))
        return;
    if (show) {
        if (!row || !LoadModSettings(row->name, &g_modSchema, &g_modSettingsIni))
            return;
        wcscpy(g_modSettingsName, row->info ? row->info->name : row->name);
    }
    g_modSettings = show;
    ShowPage(dialog, PAGE_MODS);
}

static void OfferManagerInstall(HWND dialog)
{
    if (g_modBusy)
        MsgBox(dialog, Ls(LS_MODS_BUSY), MB_OK | MB_ICONINFORMATION);
    else if (ManagerPackage()
             && MsgBox(dialog, Ls(LS_MODS_MANAGER_PROMPT), MB_YESNO | MB_ICONQUESTION) == IDYES)
        InstallReleases(dialog, ManagerPackage(), false, false);
}

static void ImportMod(HWND dialog, const wchar_t *path)
{
    wchar_t name[MOD_NAME_CAP], message[1024];
    const wchar_t *file = wcsrchr(path, L'\\');
    HCURSOR previous;
    bool ok;

    if (!g_modsAvailable) {
        OfferManagerInstall(dialog);
        return;
    }
    if (g_modBusy || !FlushMods(dialog))
        return;
    previous = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    ok = Mods_Import(g_exeDir, path, NULL, name);
    if (ok) {
        ListInstalledMod(name);
        ok = Ini_Save(&g_modsIni);
    }
    SetCursor(previous);
    ReloadMods(dialog);

    FORMAT(message, Ls(ok ? LS_MODS_INSTALLED : LS_MODS_IMPORT_FAILED),
           ok && name[0] ? name : file ? file + 1 : path);
    MsgBox(dialog, message, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

static void ImportModDialog(HWND dialog)
{
    wchar_t path[MAX_PATH] = L"";
    HMENU menu = CreatePopupMenu();
    RECT button;
    int choice;

    AppendMenuW(menu, MF_STRING, 1, Ls(LS_IMPORT_FILE));
    AppendMenuW(menu, MF_STRING, 2, Ls(LS_IMPORT_FOLDER));
    GetWindowRect(GetDlgItem(dialog, IDC_MODS_IMPORT), &button);
    choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, button.left, button.bottom,
                            0, dialog, NULL);
    DestroyMenu(menu);

    if (choice == 1) {
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = dialog;
        ofn.lpstrFilter = L"Mods (*.zip;*.dll)\0*.zip;*.dll\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn))
            return;
    } else if (choice == 2) {
        BROWSEINFOW browse;
        LPITEMIDLIST item;
        memset(&browse, 0, sizeof(browse));
        browse.hwndOwner = dialog;
        browse.ulFlags = BIF_RETURNONLYFSDIRS;
        item = SHBrowseForFolderW(&browse);
        if (!item)
            return;
        SHGetPathFromIDListW(item, path);
        CoTaskMemFree(item);
    }
    if (path[0])
        ImportMod(dialog, path);
}

static void InstallSelectedMod(HWND dialog)
{
    int index = SelectedModRow(dialog);
    const ModInfo *info = index >= 0 ? g_modRows[index].info : NULL;

    if (!info)
        return;
    if (info->repo[0]) {
        InstallReleases(dialog, info, false, false);
    } else if (info->page[0]) {
        ShellExecuteW(dialog, L"open", info->page, NULL, NULL, SW_SHOWNORMAL);
        MsgBox(dialog, Ls(LS_MODS_PAGE_OPENED), MB_OK | MB_ICONINFORMATION);
    }
}

static void MoveSelectedMod(HWND dialog, int delta)
{
    int index = SelectedModRow(dialog), other = index + delta;
    ModRow swap;

    if (index < 0 || other < 0 || other >= g_modRowCount
            || !g_modRows[index].installed || !g_modRows[other].installed)
        return;
    swap = g_modRows[index];
    g_modRows[index] = g_modRows[other];
    g_modRows[other] = swap;
    g_modsDirty = true;
    FillModsList(dialog, other);
}

static void OnModsListChanged(HWND dialog, const NMLISTVIEW *change)
{
    ModRow *row = &g_modRows[change->iItem < 0 ? 0 : change->iItem];
    UINT image = change->uNewState & LVIS_STATEIMAGEMASK;

    if (g_modsFilling || change->iItem < 0 || !(change->uChanged & LVIF_STATE))
        return;
    if ((change->uNewState ^ change->uOldState) & LVIS_SELECTED)
        UpdateModsSelection(dialog);
    if (image == (change->uOldState & LVIS_STATEIMAGEMASK))
        return;
    if (!row->installed) {
        g_modsFilling = true;
        ListView_SetItemState(change->hdr.hwndFrom, change->iItem, 0, LVIS_STATEIMAGEMASK);
        g_modsFilling = false;
        return;
    }
    row->enabled = image == INDEXTOSTATEIMAGEMASK(2);
    row->changed = true;
    g_modsDirty = true;
}

static void InitModsPage(HWND dialog)
{
    ListView_SetExtendedListViewStyle(GetDlgItem(dialog, IDC_MODS_LIST),
                                      LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    DragAcceptFiles(dialog, TRUE);
    BuildModRows();
    FillModsList(dialog, 0);
    PostMessageW(dialog, WM_APP_MODS_CHECK, 0, 0);
}

static void RefreshPadLabels(HWND dialog)
{
    const StoredBinding *bindings = CurrentPreset()->bindings;
    wchar_t text[MAX_PATH + 16];
    int used[2] = { -1, -1 }, player, i;

    for (player = 0; player < 2; ++player)
        for (i = 0; i < BINDINGS_PER_PLAYER && used[player] < 0; ++i)
            if (bindings[player * BINDINGS_PER_PLAYER + i].source != 0)
                used[player] = bindings[player * BINDINGS_PER_PLAYER + i].source - 1;

    for (player = 0; player < 2; ++player) {
        const StoredBinding *own = bindings + player * BINDINGS_PER_PLAYER;
        if (used[player] < 0 && g_dpadPad[player] && IsEmptyBinding(&own[0]) && IsEmptyBinding(&own[1])
                && IsEmptyBinding(&own[2]) && IsEmptyBinding(&own[3]))
            used[player] = g_dpadPad[player] - 1;
    }
    for (player = 0; player < 2; ++player) {
        int pad = used[player] >= 0 ? used[player]
                : used[player ^ 1] >= 0 ? used[player ^ 1] ^ 1 : player;
        if (pad < (int)g_joystickCount && g_joystickNames[pad][0])
            FORMAT(text, L"PAD%d: %hs", pad + 1, g_joystickNames[pad]);
        else
            FORMAT(text, L"PAD%d: %s", pad + 1, Ls(LS_PAD_NONE));
        SetDlgItemTextW(dialog, IDC_PAD1_NAME + player, text);
    }
}

static bool PageAvailable(int page)
{
    return page != PAGE_REVIVAL || g_revivalAvailable;
}

static void ApplyLanguage(HWND dialog)
{
    static const int kTabCaptions[PAGE_COUNT][2] = {
        { LS_TAB_KEYS, LS_TAB_KEYS },
        { LS_TAB_REVIVAL_UNAVAILABLE, LS_TAB_REVIVAL },
        { LS_TAB_MODS, LS_TAB_MODS },
    };
    static const int kCaptions[][2] = {
        { IDC_LANG_TOGGLE, LS_LANG_TOGGLE }, { IDC_MODS_SETTINGS, LS_BTN_SETTINGS },
        { IDC_MODS_BACK, LS_BTN_BACK },
        { IDC_REV_DESC_GROUP, LS_DESC_GROUP }, { IDC_MODS_DESC_GROUP, LS_DESC_GROUP },
        { IDC_MODS_UP, LS_BTN_UP }, { IDC_MODS_DOWN, LS_BTN_DOWN },
        { IDC_MODS_UPDATE_ALL, LS_BTN_UPDATE_ALL }, { IDC_MODS_IMPORT, LS_BTN_IMPORT },
    };
    HWND list = GetDlgItem(dialog, IDC_REV_LIST);
    RECT client;
    TCITEMW item;
    int i;

    for (i = 0; i < (int)ARRAYSIZE(kLangControlTexts); ++i)
        SetDlgItemTextW(dialog, kLangControlTexts[i].id, kLangControlTexts[i].text[g_lang]);
    for (i = 0; i < (int)ARRAYSIZE(kCaptions); ++i)
        SetDlgItemTextW(dialog, kCaptions[i][0], Ls(kCaptions[i][1]));
    if (!g_modBusy)
        SetModsStatus(dialog, NULL);

    SendDlgItemMessageW(dialog, IDC_PRESET_COMBO, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < CONFIG_PRESET_COUNT; ++i)
        SendDlgItemMessageW(dialog, IDC_PRESET_COMBO, CB_ADDSTRING, 0,
                            (LPARAM)kLangPresetNames[i][g_lang]);
    SendDlgItemMessageW(dialog, IDC_PRESET_COMBO, CB_SETCURSEL, g_currentPreset, 0);

    memset(&item, 0, sizeof(item));
    item.mask = TCIF_TEXT;
    for (i = 0; i < PAGE_COUNT; ++i) {
        item.pszText = (LPWSTR)Ls(kTabCaptions[i][PageAvailable(i)]);
        if (!SendDlgItemMessageW(dialog, IDC_MAIN_TAB, TCM_SETITEMW, i, (LPARAM)&item))
            SendDlgItemMessageW(dialog, IDC_MAIN_TAB, TCM_INSERTITEMW, i, (LPARAM)&item);
    }

    GetClientRect(list, &client);
    SetListColumn(list, 0, Ls(LS_REV_COL_SETTING), client.right * 45 / 100);
    SetListColumn(list, 1, Ls(LS_REV_COL_VALUE), client.right * 55 / 100);
    ListView_SetColumnWidth(list, 1, LVSCW_AUTOSIZE_USEHEADER);

    list = GetDlgItem(dialog, IDC_MODS_LIST);
    GetClientRect(list, &client);
    SetListColumn(list, 0, Ls(LS_MODS_COL_MOD), client.right * 44 / 100);
    SetListColumn(list, 1, Ls(LS_MODS_COL_TYPE), client.right * 24 / 100);
    SetListColumn(list, 2, Ls(LS_MODS_COL_STATUS), client.right * 32 / 100);
    ListView_SetColumnWidth(list, 2, LVSCW_AUTOSIZE_USEHEADER);

    RefreshPadLabels(dialog);
    RefreshBindingTexts(dialog);
    BindEditor(dialog, g_editIni == &g_modSettingsIni);
    FillModsList(dialog, SelectedModRow(dialog));
}

static int PageOfControl(int id)
{
    switch (id) {
    case IDC_MAIN_TAB: case IDC_LANG_TOGGLE: case IDC_VERSION_LINK:
    case IDC_OK_BUTTON: case IDC_CANCEL_BUTTON: case IDC_SAVE_AND_RUN:
        return PAGE_COMMON;
    }
    if (id >= IDC_REV_SECTIONS && id <= IDC_REV_DESC_GROUP)
        return PAGE_REVIVAL;
    if (id >= IDC_MODS_LIST && id <= IDC_MODS_SETTINGS)
        return PAGE_MODS;
    return PAGE_KEYS;
}

static bool ControlShown(int id, int page)
{
    bool modSettings = page == PAGE_MODS && g_modSettings;
    int owner = PageOfControl(id);

    if (id == IDC_MODS_BACK)
        return modSettings;
    if (owner == PAGE_REVIVAL)
        return page == PAGE_REVIVAL || modSettings;
    if (owner == PAGE_MODS)
        return page == PAGE_MODS && !modSettings;
    return owner == PAGE_COMMON || owner == page;
}

static BOOL CALLBACK ShowPageChildProc(HWND child, LPARAM page)
{
    if (GetParent(child) == g_configDialog)
        ShowWindow(child, ControlShown(GetDlgCtrlID(child), (int)page) ? SW_SHOW : SW_HIDE);
    return TRUE;
}

static void ShowPage(HWND dialog, int page)
{
    static const int kFirstFocus[PAGE_COUNT] = { IDC_CANCEL_BUTTON, IDC_REV_LIST, IDC_MODS_LIST };
    bool modSettings = page == PAGE_MODS && g_modSettings;

    g_activePage = page;
    if (page == PAGE_REVIVAL || modSettings)
        BindEditor(dialog, modSettings);
    EnumChildWindows(dialog, ShowPageChildProc, page);
    DisarmCapture();
    if (IsWindowVisible(dialog))
        SetFocus(GetDlgItem(dialog, modSettings ? IDC_REV_LIST : kFirstFocus[page]));
}

static void HandleMainTabNotify(HWND dialog, const NMHDR *header)
{
    HWND tab = header->hwndFrom;

    if (header->code == TCN_SELCHANGING) {
        TCHITTESTINFO hit;
        DWORD pos = GetMessagePos();
        int target;

        hit.pt.x = GET_X_LPARAM(pos);
        hit.pt.y = GET_Y_LPARAM(pos);
        hit.flags = 0;
        ScreenToClient(tab, &hit.pt);
        target = (int)SendMessageW(tab, TCM_HITTEST, 0, (LPARAM)&hit);
        if (target >= 0 && !PageAvailable(target)) {
            MessageBeep(MB_ICONASTERISK);
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
        }
    } else if (header->code == TCN_SELCHANGE) {
        int selected = (int)SendMessageW(tab, TCM_GETCURSEL, 0, 0);
        if (!PageAvailable(selected)) {
            SendMessageW(tab, TCM_SETCURSEL, g_activePage, 0);
        } else if (selected == PAGE_MODS && !g_modsAvailable) {
            SendMessageW(tab, TCM_SETCURSEL, g_activePage, 0);
            OfferManagerInstall(dialog);
        } else if (selected != g_activePage) {
            ShowPage(dialog, selected);
        }
    }
}

static bool SaveEverything(HWND dialog)
{
    WarnDPadWithoutPad(dialog);
    if (!SaveKeyConfig(dialog))
        return false;
    if (g_revivalDirty) {
        if (!Ini_Save(&g_revivalIni)) {
            MsgBox(dialog, Ls(LS_REV_SAVE_FAILED), MB_OK | MB_ICONERROR);
            return false;
        }
        g_revivalDirty = false;
    }
    if (!SaveModSettings(dialog))
        return false;
    if (g_modsDirty) {
        if (!SaveMods()) {
            MsgBox(dialog, Ls(LS_MODS_SAVE_FAILED), MB_OK | MB_ICONERROR);
            return false;
        }
        g_modsDirty = false;
    }
    return true;
}

static LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void InitConfigDialog(HWND dialog)
{
    const KeyPreset *global = &g_keyConfig.presets[0];
    HICON icon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_CONFIG));

    g_configDialog = dialog;
    SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    SelectBindingSlot(dialog, -1);
    CheckDlgButton(dialog, IDC_HIGH_RESOLUTION, global->highResolution ? BST_CHECKED : BST_UNCHECKED);
    RestoreRadioGroup(dialog, global->cpuDifficulty, 1127, 4);
    RestoreRadioGroup(dialog, global->drawRate, 1112, 2);
    RestoreRadioGroup(dialog, global->effects, 1120, 2);
    RestoreRadioGroup(dialog, global->damage, 1124, 3);
    RestoreRadioGroup(dialog, global->roundCount, 1131, 3);

    ListView_SetExtendedListViewStyle(GetDlgItem(dialog, IDC_REV_LIST), LVS_EX_FULLROWSELECT);
    ApplyLanguage(dialog);
    InitModsPage(dialog);
    ShowPage(dialog, PAGE_KEYS);
    BuildBindingGrid(dialog);
}

static INT_PTR CALLBACK ConfigDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        InitConfigDialog(dialog);
        SetDlgItemTextW(dialog, IDC_VERSION_LINK, L"v" EFZ_CONFIG_VERSION);
        return FALSE;

    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam != GetDlgItem(dialog, IDC_VERSION_LINK))
            return FALSE;
        SetTextColor((HDC)wParam, RGB(0, 0, 238));
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);

    case WM_SETCURSOR:
        if ((HWND)wParam != GetDlgItem(dialog, IDC_VERSION_LINK))
            return FALSE;
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
        return TRUE;

    case WM_ACTIVATEAPP:
        g_applicationActive = (BOOL)wParam;
        return FALSE;

    case WM_LBUTTONDOWN: {
        POINT pt;
        int slot;

        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        if (g_activePage != PAGE_KEYS)
            return FALSE;
        for (slot = 0; slot < BINDINGS_PER_PRESET; ++slot)
            if (PtInRect(&g_gridCells[slot], pt))
                SelectBindingSlot(dialog, slot);
        return FALSE;
    }

    case WM_NOTIFY: {
        const NMHDR *header = (const NMHDR *)lParam;
        const NMLISTVIEW *change = (const NMLISTVIEW *)lParam;

        switch (header->idFrom) {
        case IDC_MAIN_TAB:
            HandleMainTabNotify(dialog, header);
            return TRUE;

        case IDC_REV_LIST:
            if (header->code == NM_DBLCLK
                    || (header->code == LVN_KEYDOWN
                        && (((const NMLVKEYDOWN *)lParam)->wVKey == VK_SPACE
                            || ((const NMLVKEYDOWN *)lParam)->wVKey == VK_F2)))
                EditRevivalRow(dialog);
            else if (header->code == LVN_ITEMCHANGED && (change->uChanged & LVIF_STATE)
                     && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED))
                UpdateRevivalDescription(dialog);
            return TRUE;

        case IDC_MODS_LIST:
            if (header->code == LVN_ITEMCHANGED)
                OnModsListChanged(dialog, change);
            return TRUE;
        }
        return FALSE;
    }

    case WM_APP_MODS_CHECK:
        FetchCatalogue(dialog);
        return TRUE;

    case WM_APP_MODS_PROGRESS: {
        const ModTask *task = (const ModTask *)lParam;
        wchar_t status[256];
        if (task->kind == TASK_INSTALL)
            FORMAT(status, Ls(LS_MODS_DOWNLOADING), task->mods[wParam].name);
        else
            FORMAT(status, L"%s", Ls(LS_MODS_CHECKING));
        SetModsStatus(dialog, status);
        return TRUE;
    }

    case WM_APP_MODS_DONE:
        if (g_newerRelease)
            SetDlgItemTextW(dialog, IDC_VERSION_LINK, L"v" EFZ_CONFIG_VERSION L" (update!)");
        OnModTaskDone(dialog, (ModTask *)lParam);
        return TRUE;

    case WM_DROPFILES: {
        wchar_t path[MAX_PATH];
        UINT i, count = DragQueryFileW((HDROP)wParam, 0xFFFFFFFF, NULL, 0);
        for (i = 0; i < count; ++i)
            if (DragQueryFileW((HDROP)wParam, i, path, MAX_PATH))
                ImportMod(dialog, path);
        DragFinish((HDROP)wParam);
        return TRUE;
    }

    case WM_COMMAND:

        if (g_modBusy && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDC_OK_BUTTON
                          || LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDC_CANCEL_BUTTON
                          || LOWORD(wParam) == IDC_SAVE_AND_RUN)) {
            MsgBox(dialog, Ls(LS_MODS_BUSY), MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        switch (LOWORD(wParam)) {
        case IDOK: {
            int focus = GetDlgCtrlID(GetFocus());
            if (focus == IDC_REV_LIST) {
                EditRevivalRow(dialog);
                return TRUE;
            }
            if (focus == IDC_MODS_LIST)
                return TRUE;
        }

        case IDC_OK_BUTTON:
            if (SaveEverything(dialog))
                DestroyWindow(g_mainWindow);
            return TRUE;

        case IDCANCEL:
        case IDC_CANCEL_BUTTON:
            DestroyWindow(g_mainWindow);
            return TRUE;

        case IDC_SAVE_AND_RUN:
            if (!SaveEverything(dialog))
                return TRUE;
            if ((INT_PTR)ShellExecuteW(dialog, L"open", g_gameExePath, NULL, g_exeDir,
                                       SW_SHOWNORMAL) > 32)
                DestroyWindow(g_mainWindow);
            else
                MsgBox(dialog, Ls(LS_LAUNCH_FAILED), MB_OK | MB_ICONERROR);
            return TRUE;

        case IDC_REV_SECTIONS:
            if (HIWORD(wParam) == LBN_SELCHANGE)
                FillRevivalList(dialog, (int)SendDlgItemMessageW(dialog, IDC_REV_SECTIONS,
                                                                 LB_GETCURSEL, 0, 0));
            return TRUE;

        case IDC_MODS_UP:
            MoveSelectedMod(dialog, -1);
            return TRUE;
        case IDC_MODS_DOWN:
            MoveSelectedMod(dialog, 1);
            return TRUE;
        case IDC_MODS_INSTALL:
            InstallSelectedMod(dialog);
            return TRUE;
        case IDC_MODS_UPDATE_ALL:
            InstallReleases(dialog, NULL, true, false);
            return TRUE;
        case IDC_MODS_IMPORT:
            ImportModDialog(dialog);
            return TRUE;
        case IDC_MODS_SETTINGS:
            ShowModSettings(dialog, true);
            return TRUE;
        case IDC_MODS_BACK:
            ShowModSettings(dialog, false);
            return TRUE;

        case IDC_VERSION_LINK:
            ShellExecuteW(dialog, L"open", kReleasesUrl, NULL, NULL, SW_SHOWNORMAL);
            return TRUE;

        case IDC_LANG_TOGGLE:
            g_lang = g_lang == LANG_EN ? LANG_JA : LANG_EN;
            SaveLanguagePreference(g_lang);
            ApplyLanguage(dialog);
            return TRUE;

        case IDC_PRESET_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int selected = (int)SendDlgItemMessageW(dialog, IDC_PRESET_COMBO, CB_GETCURSEL, 0, 0);
                g_currentPreset = (uint8_t)(selected >= 0 && selected < CONFIG_PRESET_COUNT ? selected : 0);
                memset(g_dpadPad, 0, sizeof(g_dpadPad));
                RefreshBindingTexts(dialog);
                SelectBindingSlot(dialog, -1);
                SetFocus(GetDlgItem(dialog, IDC_OK_BUTTON));
            }
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

static void LocateFiles(void)
{
    wchar_t path[MAX_PATH + 64];
    HRSRC embedded = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_CATALOGUE), (LPCWSTR)RT_RCDATA);
    wchar_t *slash;

    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH - 1);
    slash = wcsrchr(g_exeDir, L'\\');
    if (slash)
        *slash = 0;
    FORMAT(g_keyIniPath, L"%s\\KEY.ini", g_exeDir);
    FORMAT(g_gameExePath, L"%s\\EFZ.exe", g_exeDir);

    FORMAT(path, L"%s\\EfzRevival.ini", g_exeDir);
    g_revivalAvailable = Ini_Load(&g_revivalIni, path);
    wcscat(path, L".efz_netplay_mod.host_protocol_recovery");
    g_protocolOverridden = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;

    FORMAT(g_modsIniPath, L"%s\\EfzModManager.ini", g_exeDir);
    g_modsAvailable = Ini_Load(&g_modsIni, g_modsIniPath);

    if (embedded)
        Mods_LoadCatalogue((const char *)LockResource(LoadResource(NULL, embedded)),
                           SizeofResource(NULL, embedded));
    if (GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH)) {
        wcscat(path, L"\\EfzConfig+");
        CreateDirectoryW(path, NULL);
        FORMAT(g_cataloguePath, L"%s\\mods_catalogue.csv", path);
        Mods_LoadCatalogueFile(g_cataloguePath);
    }
}

static void ApplyWineOverrides(void)
{
    static const wchar_t *const kDlls[] = { L"dinput", L"ddraw" };
    wchar_t overrides[1024], previous[900];
    DWORD length;
    HKEY key;
    int i;

    if (!GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version"))
        return;
    length = GetEnvironmentVariableW(L"WINEDLLOVERRIDES", previous, ARRAYSIZE(previous));
    if (length == 0 || length >= ARRAYSIZE(previous))
        previous[0] = 0;
    FORMAT(overrides, L"dinput,ddraw=n,b%s%s", previous[0] ? L";" : L"", previous);
    SetEnvironmentVariableW(L"WINEDLLOVERRIDES", overrides);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Wine\\AppDefaults\\EFZ.exe\\DllOverrides",
                        0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    for (i = 0; i < (int)ARRAYSIZE(kDlls); ++i)
        if (RegQueryValueExW(key, kDlls[i], NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            RegSetValueExW(key, kDlls[i], 0, REG_SZ, (const BYTE *)L"native,builtin",
                           sizeof(L"native,builtin"));
    RegCloseKey(key);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, LPWSTR commandLine, int showCommand)
{
    static const wchar_t kClassName[] = L"EfzConfig+";
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    MSG message;
    HWND dialog;

    (void)previousInstance;
    (void)commandLine;

    g_instance = instance;
    g_lang = LoadLanguagePreference();

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    LocateFiles();
    ApplyWineOverrides();
    if (!LoadKeyConfig())
        return 1;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_CONFIG));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc))
        return 1;
    g_mainWindow = CreateWindowExW(0, kClassName, kClassName, WS_POPUP | WS_SYSMENU,
                                   0, 0, 0, 0, NULL, NULL, instance, NULL);
    ShowWindow(g_mainWindow, showCommand);

    dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CONFIG), g_mainWindow,
                                ConfigDialogProc, 0);
    if (!dialog)
        return 1;
    if (!InitializeDirectInput(dialog)) {
        MsgBox(dialog, Ls(LS_DINPUT_FAILED), MB_OK | MB_ICONERROR);
        return 1;
    }
    RefreshPadLabels(dialog);

    for (;;) {
        if (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                break;
            if (!IsDialogMessageW(dialog, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else if (g_applicationActive && g_activePage == PAGE_KEYS) {
            PollAndAssignBinding(dialog);
            MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
        } else {
            WaitMessage();
        }
    }

    ShutdownDirectInput();
    return 0;
}
