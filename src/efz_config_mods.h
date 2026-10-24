#ifndef EFZ_CONFIG_MODS_H
#define EFZ_CONFIG_MODS_H

#include <windows.h>
#include <stdbool.h>

enum { MOD_COSMETIC, MOD_TOOL, MOD_GAMEPLAY, MOD_MANAGER };

#define MOD_NAME_CAP 64
#define MOD_TAG_CAP 32

typedef struct ModInfo {
    wchar_t name[MOD_NAME_CAP];
    wchar_t folder[MOD_NAME_CAP];
    int type;
    wchar_t author[64];
    wchar_t description[400];
    wchar_t repo[96];
    wchar_t page[200];
    wchar_t latest[MOD_TAG_CAP];
} ModInfo;

extern ModInfo g_modCatalogue[256];
extern int g_modCatalogueCount;

bool Mods_LoadCatalogue(const char *csv, size_t size);
bool Mods_LoadCatalogueFile(const wchar_t *path);

bool Mods_FetchCatalogue(const wchar_t *url, const wchar_t *cachePath);

bool Mods_CheckLatest(ModInfo *info);

void Mods_InstalledRelease(const wchar_t *gameDir, const wchar_t *folder,
                           wchar_t tag[MOD_TAG_CAP], wchar_t name[MOD_NAME_CAP]);

void Mods_AdoptInstalled(const wchar_t *gameDir, const ModInfo *info);
bool Mods_InstallRelease(const wchar_t *gameDir, ModInfo *info, wchar_t folderOut[MOD_NAME_CAP]);

bool Mods_Import(const wchar_t *gameDir, const wchar_t *path, const wchar_t *preferredName,
                 wchar_t folderOut[MOD_NAME_CAP]);

typedef struct ModSetting {
    wchar_t section[64], key[64], label[96], description[1024];
    int type;
    int minimum, maximum;
    wchar_t choices[9][32];
    int choiceCount;
    wchar_t fallback[128];
} ModSetting;

typedef struct ModSchema {
    wchar_t iniFile[MOD_NAME_CAP];
    int count;
    ModSetting *settings;
} ModSchema;

bool Mods_LoadSchema(const wchar_t *dllPath, ModSchema *schema);
void Mods_FreeSchema(ModSchema *schema);

#endif
