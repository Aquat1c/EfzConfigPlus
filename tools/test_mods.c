#include <stdio.h>
#include <string.h>

#include "../src/efz_config_mods.h"
#include "test_mods_zips.h"

static int g_failures;
static wchar_t g_game[MAX_PATH], g_work[MAX_PATH];

static void check(const char *name, bool condition)
{
    printf("%s %s\n", condition ? "ok  " : "FAIL", name);
    g_failures += !condition;
}

static const wchar_t *in(const wchar_t *base, const wchar_t *relative)
{
    static wchar_t path[4][MAX_PATH * 2];
    static int next;
    wchar_t *out = path[next++ & 3];
    _snwprintf(out, MAX_PATH * 2, L"%s\\%s", base, relative);
    return out;
}

static void put(const wchar_t *path, const void *bytes, size_t size)
{
    FILE *f = _wfopen(path, L"wb");
    fwrite(bytes, 1, size, f);
    fclose(f);
}

static const char *get(const wchar_t *path)
{
    static char text[256];
    FILE *f = _wfopen(path, L"rb");
    text[0] = 0;
    if (f) {
        text[fread(text, 1, sizeof(text) - 1, f)] = 0;
        fclose(f);
    }
    return text;
}

static const char kCsv[] =
    "banner, with a comma\r\n"
    "Name,Type,Description,Created By,Download,Folder\r\n"
    "ModA,Gameplay,\"Says \"\"hi\"\", twice\",Someone,https://github.com/owner/repo/releases,mod_a\r\n"
    "ModB,Cosmetic,Plain,Other,see https://example.org/file/x,\r\n"
    "Loader,Manager,x,y,Pre-installed,\n";

int main(int argc, char **argv)
{
    wchar_t name[MOD_NAME_CAP], tag[MOD_TAG_CAP], temp[MAX_PATH], dll[MAX_PATH] = L"";
    ModSchema schema;

    GetTempPathW(MAX_PATH, temp);
    _snwprintf(g_work, MAX_PATH, L"%sefz_config_test_%lu", temp, GetCurrentProcessId());
    CreateDirectoryW(g_work, NULL);
    _snwprintf(g_game, MAX_PATH, L"%s\\game", g_work);
    wcscat(g_work, L"\\work");
    CreateDirectoryW(g_game, NULL);
    CreateDirectoryW(g_work, NULL);
    CreateDirectoryW(in(g_game, L"system"), NULL);

    put(in(g_work, L"Lone Mod.dll"), "MZ", 2);
    check("dll import", Mods_Import(g_game, in(g_work, L"Lone Mod.dll"), NULL, name)
                        && wcscmp(name, L"LoneMod") == 0
                        && strcmp(get(in(g_game, L"mods\\LoneMod\\LoneMod.dll")), "MZ") == 0);

    put(in(g_work, L"download.zip"), kZipWrapped, sizeof(kZipWrapped));
    CreateDirectoryW(in(g_game, L"mods\\CoolMod"), NULL);
    put(in(g_game, L"mods\\CoolMod\\CoolMod.ini"), "user=1", 6);
    check("wrapped zip", Mods_Import(g_game, in(g_work, L"download.zip"), NULL, name)
                         && wcscmp(name, L"CoolMod") == 0
                         && strcmp(get(in(g_game, L"mods\\CoolMod\\CoolMod.dll")), "MZ-cool") == 0
                         && strlen(get(in(g_game, L"mods\\CoolMod\\system\\a.dat"))) == 40
                         && strcmp(get(in(g_game, L"mods\\CoolMod\\CoolMod.ini")), "user=1") == 0);

    put(in(g_work, L"FlatMod.zip"), kZipFlat, sizeof(kZipFlat));
    check("flat zip", Mods_Import(g_game, in(g_work, L"FlatMod.zip"), NULL, name)
                      && wcscmp(name, L"FlatMod") == 0
                      && strcmp(get(in(g_game, L"mods\\FlatMod\\system\\b.dat")), "data") == 0);
    check("preferred name", Mods_Import(g_game, in(g_work, L"FlatMod.zip"), L"Forced", name)
                         && wcscmp(name, L"Forced") == 0
                         && strcmp(get(in(g_game, L"mods\\Forced\\readme.txt")), "hi") == 0);

    put(in(g_work, L"pkg.zip"), kZipPackage, sizeof(kZipPackage));
    check("package zip", Mods_Import(g_game, in(g_work, L"pkg.zip"), NULL, name) && name[0] == 0
                         && strcmp(get(in(g_game, L"dinput.dll")), "MZ-loader") == 0
                         && strcmp(get(in(g_game, L"EfzModManager.ini")), "[Mods]\r\n") == 0
                         && strcmp(get(in(g_game, L"mods\\README.txt")), "readme") == 0);

    check("folder import", Mods_Import(g_game, in(g_game, L"mods\\FlatMod"), L"Copy", name)
                           && strcmp(get(in(g_game, L"mods\\Copy\\system\\b.dat")), "data") == 0);

    put(in(g_work, L"evil.zip"), kZipEvil, sizeof(kZipEvil));
    check("zip path escape refused", !Mods_Import(g_game, in(g_work, L"evil.zip"), NULL, name));
    check("unknown file refused", !Mods_Import(g_game, in(g_work, L"nothing.rar"), NULL, name));

    Mods_InstalledRelease(g_game, L"CoolMod", tag, name);
    check("no recorded release", tag[0] == 0 && name[0] == 0);

    check("catalogue", Mods_LoadCatalogue(kCsv, sizeof(kCsv) - 1) && g_modCatalogueCount == 3
                       && g_modCatalogue[0].type == MOD_GAMEPLAY
                       && wcscmp(g_modCatalogue[0].folder, L"mod_a") == 0
                       && wcscmp(g_modCatalogue[0].repo, L"owner/repo") == 0
                       && wcscmp(g_modCatalogue[0].description, L"Says \"hi\", twice") == 0
                       && wcscmp(g_modCatalogue[1].folder, L"ModB") == 0
                       && wcscmp(g_modCatalogue[1].page, L"https://example.org/file/x") == 0
                       && g_modCatalogue[2].type == MOD_MANAGER
                       && !g_modCatalogue[2].repo[0] && !g_modCatalogue[2].page[0]);
    check("not a catalogue", !Mods_LoadCatalogue("<html>Sign in</html>", 20) && g_modCatalogueCount == 3);

    if (argc > 1)
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, dll, MAX_PATH);
    check("schema", Mods_LoadSchema(dll, &schema) && schema.count == 3
                    && wcscmp(schema.iniFile, L"TestMod.ini") == 0
                    && wcscmp(schema.settings[0].label, L"Enabled") == 0
                    && wcscmp(schema.settings[0].description, L"Turns the mod on.\nSecond line.") == 0
                    && schema.settings[1].type == 1 && schema.settings[1].maximum == 400
                    && schema.settings[1].label[0] == 0
                    && schema.settings[2].choiceCount == 3
                    && wcscmp(schema.settings[2].choices[2], L"Sharp") == 0
                    && wcscmp(schema.settings[2].fallback, L"Linear") == 0);
    Mods_FreeSchema(&schema);
    GetModuleFileNameW(NULL, dll, MAX_PATH);
    check("no schema", !Mods_LoadSchema(dll, &schema) && !Mods_LoadSchema(L"missing.dll", &schema));

    if (argc > 2) {
        wchar_t url[512];
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, url, 512);
        g_modCatalogueCount = 0;
        check("catalogue download", Mods_FetchCatalogue(url, in(g_work, L"cache.csv"))
                                    && Mods_LoadCatalogueFile(in(g_work, L"cache.csv")));
        printf("     %d entries, first: %ls (%ls)\n", g_modCatalogueCount,
               g_modCatalogue[0].name, g_modCatalogue[0].repo);
    }

    if (argc > 5) {
        ModInfo mod;
        memset(&mod, 0, sizeof(mod));
        MultiByteToWideChar(CP_ACP, 0, argv[3], -1, mod.repo, 96);
        MultiByteToWideChar(CP_ACP, 0, argv[4], -1, mod.folder, 64);
        wcscpy(mod.name, mod.folder);
        MultiByteToWideChar(CP_ACP, 0, argv[5], -1, dll, MAX_PATH);
        CreateDirectoryW(in(g_game, L"mods"), NULL);
        _snwprintf(temp, MAX_PATH, L"%s\\mods\\%s", g_game, mod.folder);
        CreateDirectoryW(temp, NULL);
        _snwprintf(temp, MAX_PATH, L"%s\\mods\\%s\\%s.dll", g_game, mod.folder, mod.folder);
        check("adopt: latest tag", Mods_CheckLatest(&mod));
        CopyFileW(dll, temp, FALSE);
        Mods_AdoptInstalled(g_game, &mod);
        Mods_InstalledRelease(g_game, mod.folder, tag, name);
        printf("     same file -> recorded tag '%ls'\n", tag);
        check("adopt: identical dll recorded", wcscmp(tag, mod.latest) == 0);
        _snwprintf(temp, MAX_PATH, L"%s\\mods\\%s\\efz_config.release", g_game, mod.folder);
        DeleteFileW(temp);
        _snwprintf(temp, MAX_PATH, L"%s\\mods\\%s\\%s.dll", g_game, mod.folder, mod.folder);
        put(temp, "MZ-other", 8);
        Mods_AdoptInstalled(g_game, &mod);
        Mods_InstalledRelease(g_game, mod.folder, tag, name);
        check("adopt: different dll not recorded", tag[0] == 0);
    }

    printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures != 0;
}
