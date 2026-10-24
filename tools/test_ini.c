#include <stdio.h>
#include <string.h>

#include "../src/efz_config_ini.h"

static int g_failures;
static wchar_t g_path[MAX_PATH];

static void put(const void *bytes, size_t size)
{
    FILE *f = _wfopen(g_path, L"wb");
    fwrite(bytes, 1, size, f);
    fclose(f);
}

static void expect(const char *name, const void *bytes, size_t size)
{
    static char got[4096];
    FILE *f = _wfopen(g_path, L"rb");
    size_t n = fread(got, 1, sizeof(got), f);
    fclose(f);
    if (n != size || memcmp(got, bytes, size) != 0) {
        printf("FAIL %s (%u bytes, expected %u)\n", name, (unsigned)n, (unsigned)size);
        ++g_failures;
    } else {
        printf("ok   %s\n", name);
    }
}

static void check(const char *name, bool condition)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++g_failures;
    }
}

#define UTF16(text) L"\xFEFF" text
#define WSIZE(literal) (sizeof(literal) - sizeof(wchar_t))
#define ASIZE(literal) (sizeof(literal) - 1)

int main(void)
{
    static const wchar_t revival[] = UTF16(
        L";/* banner */\r\n[Network]\r\n; * The name.\r\nName                 = EfzPlayer\r\n"
        L"MaxRollback          = 4  \r\n\r\n[Practice]\r\nPause = DIK_SPACE\r\n");
    static const wchar_t revival_set[] = UTF16(
        L";/* banner */\r\n[Network]\r\n; * The name.\r\nName                 = Tester\r\n"
        L"MaxRollback          = 12  \r\n\r\n[Practice]\r\nPause = DIK_F5\r\n");
    static const char mods[] =
        ";comment\n[Mods]\r\nEFZ2v2=1\r\nMayuOldStage=0\n\n;keep last\nEnglishTranslation=0";
    static const char mods_top[] =
        ";comment\n[Mods]\r\nNewMod=1\nEFZ2v2=0\r\nMayuOldStage=0\n\n;keep last\nEnglishTranslation=0";
    static const char mods_end[] =
        ";comment\n[Mods]\r\nEFZ2v2=1\r\nMayuOldStage=0\n\n;keep last\nEnglishTranslation=0\nLast=1";
    static const char mods_section[] =
        ";comment\n[Mods]\r\nEFZ2v2=1\r\nMayuOldStage=0\n\n;keep last\nEnglishTranslation=0\n\n[New]\nKey=v";
    IniFile ini;
    wchar_t key[64], value[64];

    GetTempPathW(MAX_PATH, g_path);
    wcscat(g_path, L"efz_config_test.ini");

    put(revival, WSIZE(revival));
    check("load utf16", Ini_Load(&ini, g_path) && ini.encoding == INI_ENC_UTF16LE);
    check("save", Ini_Save(&ini));
    expect("utf16 round-trip", revival, WSIZE(revival));

    check("parse", Ini_ParseLine(&ini, Ini_FindKey(&ini, L"Network", L"MaxRollback"),
                                 key, 64, value, 64)
                   && wcscmp(key, L"MaxRollback") == 0 && wcscmp(value, L"4") == 0);
    check("find scoped to section", Ini_FindKey(&ini, L"Network", L"Pause") < 0);
    check("set", Ini_SetValue(&ini, L"Network", L"Name", L"Tester")
                 && Ini_SetValue(&ini, L"network", L"maxrollback", L"12")
                 && Ini_SetValue(&ini, L"Practice", L"Pause", L"DIK_F5")
                 && !Ini_SetValue(&ini, L"Practice", L"Missing", L"1"));
    check("save", Ini_Save(&ini));
    expect("utf16 set keeps alignment", revival_set, WSIZE(revival_set));
    Ini_Free(&ini);

    put(mods, ASIZE(mods));
    check("load ansi", Ini_Load(&ini, g_path) && ini.encoding != INI_ENC_UTF16LE);
    check("insert top", Ini_InsertKey(&ini, L"Mods", L"NewMod", L"1", true)
                        && Ini_SetValue(&ini, L"Mods", L"EFZ2v2", L"0") && Ini_Save(&ini));
    expect("ansi insert at top", mods_top, ASIZE(mods_top));
    Ini_Free(&ini);

    put(mods, ASIZE(mods));
    check("insert end", Ini_Load(&ini, g_path)
                        && Ini_InsertKey(&ini, L"Mods", L"Last", L"1", false) && Ini_Save(&ini));
    expect("ansi insert after last entry", mods_end, ASIZE(mods_end));
    Ini_Free(&ini);

    put(mods, ASIZE(mods));
    check("new section", Ini_Load(&ini, g_path)
                         && Ini_InsertKey(&ini, L"New", L"Key", L"v", false)
                         && Ini_FindKey(&ini, L"New", L"Key") >= 0 && Ini_Save(&ini));
    expect("ansi new section", mods_section, ASIZE(mods_section));
    Ini_Free(&ini);

    DeleteFileW(g_path);
    check("missing file", !Ini_Load(&ini, g_path));
    ini.encoding = INI_ENC_ANSI;
    ini.crlf = true;
    check("new file", Ini_InsertKey(&ini, L"A", L"k", L"1", false)
                      && Ini_InsertKey(&ini, L"A", L"j", L"2", false) && Ini_Save(&ini));
    Ini_Free(&ini);
    check("new file reloads", Ini_Load(&ini, g_path) && Ini_FindKey(&ini, L"A", L"j") == 2
                              && ini.encoding != INI_ENC_UTF16LE);
    Ini_Free(&ini);
    DeleteFileW(g_path);

    printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures != 0;
}
