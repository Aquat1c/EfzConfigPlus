#include "efz_config_mods.h"

#include <shlobj.h>
#include <shellapi.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/efz_config_api.h"
#include "../third_party/miniz/miniz.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
#define FORMAT(buffer, ...) \
    (_snwprintf((buffer), COUNT_OF(buffer), __VA_ARGS__), (buffer)[COUNT_OF(buffer) - 1] = 0)

#define LONG_PATH (MAX_PATH * 2)

ModInfo g_modCatalogue[256];
int g_modCatalogueCount;

enum { COL_NAME, COL_TYPE, COL_DESCRIPTION, COL_AUTHOR, COL_DOWNLOAD, COL_FOLDER, COL_COUNT };
static const char *const kColumnNames[COL_COUNT] = {
    "Name", "Type", "Description", "Created By", "Download", "Folder",
};

#define CSV_FIELDS 8
#define CSV_FIELD_CAP 1024

static void ModNameFromPath(const wchar_t *path, wchar_t name[MOD_NAME_CAP]);

static void Widen(const char *utf8, wchar_t *out, int cap)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap))
        out[cap - 1] = 0;
}

static void ParseModRow(char fields[CSV_FIELDS][CSV_FIELD_CAP], const int *columns, ModInfo *info)
{
    const char *cell[COL_COUNT];
    const char *link;
    wchar_t wide[CSV_FIELD_CAP];
    int i;

    for (i = 0; i < COL_COUNT; ++i)
        cell[i] = columns[i] >= 0 ? fields[columns[i]] : "";
    memset(info, 0, sizeof(*info));
    Widen(cell[COL_NAME], info->name, MOD_NAME_CAP);
    Widen(cell[COL_AUTHOR], info->author, (int)COUNT_OF(info->author));
    Widen(cell[COL_DESCRIPTION], info->description, (int)COUNT_OF(info->description));
    Widen(cell[COL_FOLDER][0] ? cell[COL_FOLDER] : cell[COL_NAME], wide, CSV_FIELD_CAP);
    ModNameFromPath(wide, info->folder);

    switch (cell[COL_TYPE][0] | 0x20) {
    case 'c': info->type = MOD_COSMETIC; break;
    case 'g': info->type = MOD_GAMEPLAY; break;
    case 'm': info->type = MOD_MANAGER; break;
    default:  info->type = MOD_TOOL; break;
    }

    link = strstr(cell[COL_DOWNLOAD], "https://");
    if (link && strncmp(link, "https://github.com/", 19) == 0) {
        size_t owner = strcspn(link + 19, "/ ");
        size_t repo = link[19 + owner] == '/' ? strcspn(link + 19 + owner + 1, "/ ") : 0;
        if (owner && repo && owner + 1 + repo < COUNT_OF(info->repo))
            MultiByteToWideChar(CP_UTF8, 0, link + 19, (int)(owner + 1 + repo),
                                info->repo, (int)COUNT_OF(info->repo) - 1);
    } else if (link) {
        size_t length = strcspn(link, " \t");
        if (length < COUNT_OF(info->page))
            MultiByteToWideChar(CP_UTF8, 0, link, (int)length, info->page,
                                (int)COUNT_OF(info->page) - 1);
    }
}

static int ParseCatalogue(const char *csv, size_t size, ModInfo *out, int cap)
{
    static char fields[CSV_FIELDS][CSV_FIELD_CAP];
    int columns[COL_COUNT], field = 0, count = 0, i, c;
    size_t length = 0, at;
    bool quoted = false, header = false;

    if (size >= 3 && memcmp(csv, "\xEF\xBB\xBF", 3) == 0) {
        csv += 3;
        size -= 3;
    }
    memset(fields, 0, sizeof(fields));
    for (at = 0; at <= size; ++at) {
        char ch = at < size ? csv[at] : '\n';

        if (quoted && ch == '"' && at + 1 < size && csv[at + 1] == '"')
            ++at;
        else if (ch == '"') {
            quoted = !quoted;
            continue;
        } else if (!quoted && ch == ',') {
            field += field < CSV_FIELDS;
            length = 0;
            continue;
        } else if (!quoted && ch == '\r') {
            continue;
        } else if (!quoted && ch == '\n') {
            if (!header) {
                for (c = 0; c < COL_COUNT; ++c) {
                    columns[c] = -1;
                    for (i = 0; i < CSV_FIELDS; ++i)
                        if (_stricmp(fields[i], kColumnNames[c]) == 0)
                            columns[c] = i;
                }
                header = columns[COL_NAME] == 0 && columns[COL_DOWNLOAD] > 0;
            } else if (fields[columns[COL_NAME]][0] && count < cap) {
                ParseModRow(fields, columns, &out[count]);
                count += out[count].folder[0] != 0;
            }
            memset(fields, 0, sizeof(fields));
            field = 0;
            length = 0;
            continue;
        }
        if (field < CSV_FIELDS && length < CSV_FIELD_CAP - 1)
            fields[field][length++] = ch;
    }
    return header ? count : 0;
}

bool Mods_LoadCatalogue(const char *csv, size_t size)
{
    static ModInfo parsed[COUNT_OF(g_modCatalogue)];
    int count = ParseCatalogue(csv, size, parsed, (int)COUNT_OF(parsed));

    if (count == 0)
        return false;
    memcpy(g_modCatalogue, parsed, (size_t)count * sizeof(ModInfo));
    g_modCatalogueCount = count;
    return true;
}

static char *ReadWholeFile(const wchar_t *path, DWORD *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD read = 0;
    char *bytes;

    if (file == INVALID_HANDLE_VALUE)
        return NULL;
    *size = GetFileSize(file, NULL);
    bytes = *size != INVALID_FILE_SIZE && *size ? (char *)malloc(*size) : NULL;
    if (bytes && !(ReadFile(file, bytes, *size, &read, NULL) && read == *size)) {
        free(bytes);
        bytes = NULL;
    }
    CloseHandle(file);
    return bytes;
}

bool Mods_LoadCatalogueFile(const wchar_t *path)
{
    DWORD size;
    char *bytes = ReadWholeFile(path, &size);
    bool ok = bytes && Mods_LoadCatalogue(bytes, size);

    free(bytes);
    return ok;
}

static bool IsDirectory(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool HasExtension(const wchar_t *path, const wchar_t *extension)
{
    size_t len = wcslen(path), ext = wcslen(extension);
    return len > ext && _wcsicmp(path + len - ext, extension) == 0;
}

static bool MakeDirectories(const wchar_t *path)
{
    int rc = SHCreateDirectoryExW(NULL, path, NULL);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

static void DeleteTree(const wchar_t *path)
{
    wchar_t child[LONG_PATH];
    WIN32_FIND_DATAW found;
    HANDLE find;

    FORMAT(child, L"%s\\*", path);
    find = FindFirstFileW(child, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0)
                continue;
            FORMAT(child, L"%s\\%s", path, found.cFileName);
            if (found.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(child, found.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                DeleteFileW(child);
            else if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                RemoveDirectoryW(child);
            else
                DeleteTree(child);
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(path);
}

static bool CopyTree(const wchar_t *src, const wchar_t *dst)
{
    wchar_t from[LONG_PATH], to[LONG_PATH];
    WIN32_FIND_DATAW found;
    HANDLE find;
    bool ok = MakeDirectories(dst);

    FORMAT(from, L"%s\\*", src);
    find = ok ? FindFirstFileW(from, &found) : INVALID_HANDLE_VALUE;
    if (find == INVALID_HANDLE_VALUE)
        return false;
    do {
        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0)
            continue;
        FORMAT(from, L"%s\\%s", src, found.cFileName);
        FORMAT(to, L"%s\\%s", dst, found.cFileName);
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ok = CopyTree(from, to);
        else if (!(HasExtension(to, L".ini") && GetFileAttributesW(to) != INVALID_FILE_ATTRIBUTES))
            ok = CopyFileW(from, to, FALSE) != 0;
    } while (ok && FindNextFileW(find, &found));
    FindClose(find);
    return ok;
}

static bool MakeTempDir(wchar_t *path, size_t cap)
{
    wchar_t temp[MAX_PATH];

    if (!GetTempPathW(MAX_PATH, temp))
        return false;
    _snwprintf(path, cap, L"%sefz_config_%lu_%lu", temp, GetCurrentProcessId(), GetTickCount());
    path[cap - 1] = 0;
    return CreateDirectoryW(path, NULL) != 0;
}

static size_t WriteToHandle(void *opaque, mz_uint64 offset, const void *data, size_t size)
{
    DWORD written = 0;
    (void)offset;
    return WriteFile((HANDLE)opaque, data, (DWORD)size, &written, NULL) ? written : 0;
}

static bool Unzip(const wchar_t *zipPath, const wchar_t *destDir)
{
    HANDLE file = CreateFileW(zipPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, read = 0;
    void *bytes;
    mz_zip_archive zip;
    mz_uint i;
    bool ok;

    if (file == INVALID_HANDLE_VALUE)
        return false;
    size = GetFileSize(file, NULL);
    bytes = size != INVALID_FILE_SIZE && size ? malloc(size) : NULL;
    ok = bytes && ReadFile(file, bytes, size, &read, NULL) && read == size;
    CloseHandle(file);
    memset(&zip, 0, sizeof(zip));
    ok = ok && mz_zip_reader_init_mem(&zip, bytes, size, 0);

    for (i = 0; ok && i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat stat;
        wchar_t name[MAX_PATH], path[LONG_PATH], *p, *slash;
        HANDLE out;

        ok = mz_zip_reader_file_stat(&zip, i, &stat) != 0;
        if (!ok)
            break;

        if (!MultiByteToWideChar((stat.m_bit_flag & 0x800) ? CP_UTF8 : CP_OEMCP, 0,
                                 stat.m_filename, -1, name, MAX_PATH))
            continue;
        for (p = name; *p; ++p)
            if (*p == L'/')
                *p = L'\\';

        if (name[0] == L'\\' || wcschr(name, L':') || wcsstr(name, L"..")) {
            ok = false;
            break;
        }
        FORMAT(path, L"%s\\%s", destDir, name);
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            ok = MakeDirectories(path);
            continue;
        }
        slash = wcsrchr(path, L'\\');
        *slash = 0;
        ok = MakeDirectories(path);
        *slash = L'\\';
        out = ok ? CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL)
                 : INVALID_HANDLE_VALUE;
        ok = out != INVALID_HANDLE_VALUE;
        if (ok) {
            ok = mz_zip_reader_extract_to_callback(&zip, i, WriteToHandle, out, 0) != 0;
            CloseHandle(out);
        }
    }
    mz_zip_reader_end(&zip);
    free(bytes);
    return ok;
}

static void ModNameFromPath(const wchar_t *path, wchar_t name[MOD_NAME_CAP])
{
    const wchar_t *base = wcsrchr(path, L'\\');
    size_t n = 0;

    base = base ? base + 1 : path;
    for (; *base && n < MOD_NAME_CAP - 1; ++base)
        if (!wcschr(L" \t=[];\\/:*?\"<>|", *base))
            name[n++] = *base;
    name[n] = 0;
    if (n > 4 && (HasExtension(name, L".zip") || HasExtension(name, L".dll")))
        name[n - 4] = 0;
}

static bool ImportFolder(const wchar_t *gameDir, const wchar_t *folder, bool mayHaveWrapper,
                         const wchar_t *preferredName, wchar_t name[MOD_NAME_CAP])
{
    wchar_t root[LONG_PATH], path[LONG_PATH], onlyDir[MAX_PATH] = L"", onlyDll[MAX_PATH] = L"";
    WIN32_FIND_DATAW found;
    HANDLE find;
    int pass, dirs, files, dlls;
    bool package;

    FORMAT(root, L"%s", folder);
    for (pass = 0; pass < 2; ++pass) {
        dirs = files = dlls = 0;
        FORMAT(path, L"%s\\*", root);
        find = FindFirstFileW(path, &found);
        if (find == INVALID_HANDLE_VALUE)
            return false;
        do {
            if (found.cFileName[0] == L'.')
                continue;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ++dirs;
                wcscpy(onlyDir, found.cFileName);
            } else {
                ++files;
                if (HasExtension(found.cFileName, L".dll")) {
                    ++dlls;
                    wcscpy(onlyDll, found.cFileName);
                }
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);

        FORMAT(path, L"%s\\%s", gameDir, onlyDir);
        if (pass == 1 || !mayHaveWrapper || dirs != 1 || files != 0 || IsDirectory(path))
            break;
        ModNameFromPath(onlyDir, name);
        wcscat(root, L"\\");
        wcscat(root, onlyDir);
    }

    FORMAT(path, L"%s\\mods", root);
    package = IsDirectory(path);
    FORMAT(path, L"%s\\dinput.dll", root);
    if (package || GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        name[0] = 0;
        return CopyTree(root, gameDir);
    }

    if (dlls == 1)
        ModNameFromPath(onlyDll, name);
    else if (preferredName)
        ModNameFromPath(preferredName, name);
    if (!name[0])
        return false;
    FORMAT(path, L"%s\\mods\\%s", gameDir, name);
    return CopyTree(root, path);
}

bool Mods_Import(const wchar_t *gameDir, const wchar_t *path, const wchar_t *preferredName,
                 wchar_t name[MOD_NAME_CAP])
{
    wchar_t target[LONG_PATH], temp[MAX_PATH];
    bool ok;

    ModNameFromPath(path, name);
    if (IsDirectory(path))
        return ImportFolder(gameDir, path, false, preferredName, name);

    if (HasExtension(path, L".dll")) {
        if (preferredName)
            ModNameFromPath(preferredName, name);
        if (!name[0])
            return false;
        FORMAT(target, L"%s\\mods\\%s", gameDir, name);
        if (!MakeDirectories(target))
            return false;
        FORMAT(target, L"%s\\mods\\%s\\%s.dll", gameDir, name, name);
        return CopyFileW(path, target, FALSE) != 0;
    }

    if (!HasExtension(path, L".zip") || !MakeTempDir(temp, MAX_PATH))
        return false;
    ok = Unzip(path, temp) && ImportFolder(gameDir, temp, true, preferredName, name);
    DeleteTree(temp);
    return ok;
}

static HINTERNET OpenUrl(HINTERNET *session, const wchar_t *url, DWORD flags)
{
    *session = InternetOpenW(L"EfzConfig+", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!*session)
        return NULL;
    return InternetOpenUrlW(*session, url, NULL, 0,
                            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
                            | INTERNET_FLAG_NO_UI | flags, 0);
}

static bool HttpGet(const wchar_t *url, const wchar_t *file, char **text, size_t *textSize)
{
    enum { CHUNK = 65536, TEXT_CAP = 4 << 20 };
    HINTERNET session, request = OpenUrl(&session, url, 0);
    DWORD status = 0, size = sizeof(status), read, written;
    HANDLE out = INVALID_HANDLE_VALUE;
    char *buffer = (char *)malloc(CHUNK + 1);
    size_t total = 0;
    bool ok = buffer && request
        && HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, NULL)
        && status == 200;

    if (ok && file) {
        out = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        ok = out != INVALID_HANDLE_VALUE;
    }
    while (ok) {
        ok = InternetReadFile(request, buffer + (file ? 0 : total), CHUNK, &read) != 0;
        if (!ok || read == 0)
            break;
        total += read;
        if (file) {
            ok = WriteFile(out, buffer, read, &written, NULL) && written == read;
        } else {
            char *grown = total <= TEXT_CAP ? (char *)realloc(buffer, total + CHUNK + 1) : NULL;
            ok = grown != NULL;
            if (grown)
                buffer = grown;
        }
    }
    if (out != INVALID_HANDLE_VALUE)
        CloseHandle(out);
    if (request)
        InternetCloseHandle(request);
    if (session)
        InternetCloseHandle(session);

    if (ok && !file) {
        buffer[total] = 0;
        *text = buffer;
        *textSize = total;
    } else {
        free(buffer);
    }
    return ok;
}

bool Mods_FetchCatalogue(const wchar_t *url, const wchar_t *cachePath)
{
    static ModInfo scratch[COUNT_OF(g_modCatalogue)];
    wchar_t temp[LONG_PATH];
    char *csv = NULL;
    size_t size = 0;
    DWORD written = 0;
    HANDLE file;
    bool ok;

    if (!HttpGet(url, NULL, &csv, &size))
        return false;
    ok = ParseCatalogue(csv, size, scratch, (int)COUNT_OF(scratch)) > 0;
    if (ok) {
        FORMAT(temp, L"%s.tmp", cachePath);
        file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        ok = file != INVALID_HANDLE_VALUE;
        if (ok) {
            ok = WriteFile(file, csv, (DWORD)size, &written, NULL) && written == size;
            CloseHandle(file);
            ok = ok && MoveFileExW(temp, cachePath, MOVEFILE_REPLACE_EXISTING);
        }
    }
    free(csv);
    return ok;
}

bool Mods_CheckLatest(ModInfo *info)
{
    wchar_t url[512], location[512] = L"";
    DWORD size = sizeof(location);
    HINTERNET session, request;
    const wchar_t *found;
    bool ok = false;

    FORMAT(url, L"https://github.com/%s/releases/latest", info->repo);
    request = OpenUrl(&session, url, INTERNET_FLAG_NO_AUTO_REDIRECT);
    if (request) {
        if (HttpQueryInfoW(request, HTTP_QUERY_LOCATION, location, &size, NULL)
                && (found = wcsstr(location, L"/releases/tag/")) != NULL
                && wcslen(found + 14) < MOD_TAG_CAP) {
            wcscpy(info->latest, found + 14);
            ok = true;
        }
        InternetCloseHandle(request);
    }
    if (session)
        InternetCloseHandle(session);
    return ok;
}

static void ReleaseFilePath(const wchar_t *gameDir, const wchar_t *folder, wchar_t *path, size_t cap)
{
    _snwprintf(path, cap, L"%s\\mods\\%s\\efz_config.release", gameDir, folder);
    path[cap - 1] = 0;
}

void Mods_InstalledRelease(const wchar_t *gameDir, const wchar_t *folder,
                           wchar_t tag[MOD_TAG_CAP], wchar_t name[MOD_NAME_CAP])
{
    wchar_t path[LONG_PATH];

    ReleaseFilePath(gameDir, folder, path, LONG_PATH);
    GetPrivateProfileStringW(L"Release", L"Tag", L"", tag, MOD_TAG_CAP, path);
    GetPrivateProfileStringW(L"Release", L"Name", L"", name, MOD_NAME_CAP, path);
}

#ifndef CALG_SHA_256
#define CALG_SHA_256 0x0000800C
#endif

static void FileSha256(const wchar_t *path, char hex[65])
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE buffer[16384], digest[32];
    DWORD read, size = sizeof(digest), i;
    bool ok = file != INVALID_HANDLE_VALUE
              && CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)
              && CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);

    hex[0] = 0;
    while (ok && ReadFile(file, buffer, sizeof(buffer), &read, NULL) && read)
        ok = CryptHashData(hash, buffer, read, 0) != 0;
    if (ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0))
        for (i = 0; i < 32; ++i)
            sprintf(hex + i * 2, "%02x", digest[i]);
    if (hash)
        CryptDestroyHash(hash);
    if (provider)
        CryptReleaseContext(provider, 0);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
}

static bool ReleaseAssets(const wchar_t *repo, const wchar_t *tag, char zip[512], char dll[512], char dllSha[65])
{
    static const char marker[] = "/releases/download/";
    wchar_t url[512];
    char *html = NULL;
    const char *found, *start, *end, *sha, *next;
    size_t size, length;

    zip[0] = dll[0] = dllSha[0] = 0;
    FORMAT(url, L"https://github.com/%s/releases/expanded_assets/%s", repo, tag);
    if (!HttpGet(url, NULL, &html, &size))
        return false;
    for (found = html; (found = strstr(found, marker)) != NULL; found = end) {
        for (start = found; start > html && start[-1] != '"'; --start)
            ;
        end = strchr(found, '"');
        if (!end)
            break;
        length = (size_t)(end - start);
        if (*start != '/' || length < 5 || length > 480)
            continue;
        next = strstr(end, marker);
        sha = strstr(end, "sha256:");
        if (_strnicmp(end - 4, ".zip", 4) == 0 && !zip[0]) {
            sprintf(zip, "https://github.com%.*s", (int)length, start);
        } else if (_strnicmp(end - 4, ".dll", 4) == 0 && !dll[0]) {
            sprintf(dll, "https://github.com%.*s", (int)length, start);
            if (sha && (!next || sha < next) && strspn(sha + 7, "0123456789abcdef") >= 64) {
                memcpy(dllSha, sha + 7, 64);
                dllSha[64] = 0;
            }
        }
    }
    free(html);
    return zip[0] || dll[0];
}

void Mods_AdoptInstalled(const wchar_t *gameDir, const ModInfo *info)
{
    wchar_t path[LONG_PATH], record[LONG_PATH], tag[MOD_TAG_CAP], name[MOD_NAME_CAP];
    char zip[512], dll[512], sha[65], hex[65];

    Mods_InstalledRelease(gameDir, info->folder, tag, name);
    ReleaseFilePath(gameDir, info->folder, record, LONG_PATH);
    FORMAT(path, L"%s\\mods\\%s\\%s.dll", gameDir, info->folder, info->folder);
    if (tag[0] || !info->latest[0] || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return;
    GetPrivateProfileStringW(L"Release", L"Checked", L"", tag, MOD_TAG_CAP, record);
    if (wcscmp(tag, info->latest) == 0 || !ReleaseAssets(info->repo, info->latest, zip, dll, sha))
        return;
    FileSha256(path, hex);
    if (sha[0] && _stricmp(hex, sha) == 0) {
        WritePrivateProfileStringW(L"Release", L"Tag", info->latest, record);
        WritePrivateProfileStringW(L"Release", L"Name", info->name, record);
    } else {
        WritePrivateProfileStringW(L"Release", L"Checked", info->latest, record);
    }
}

bool Mods_InstallRelease(const wchar_t *gameDir, ModInfo *info, wchar_t folder[MOD_NAME_CAP])
{
    wchar_t url[600], temp[MAX_PATH], path[LONG_PATH], tag[MOD_TAG_CAP];
    char zip[512], dll[512], sha[65];
    const char *asset;
    size_t size;
    bool ok;

    folder[0] = 0;
    if (!Mods_CheckLatest(info) || !ReleaseAssets(info->repo, info->latest, zip, dll, sha))
        return false;
    asset = zip[0] ? zip : dll;
    wcscpy(tag, info->latest);
    if (!MakeTempDir(temp, MAX_PATH))
        return false;

    Widen(asset, url, (int)COUNT_OF(url));
    FORMAT(path, L"%s%s", temp, wcsrchr(url, L'/'));
    for (size = wcslen(temp); path[size]; ++size)
        if (path[size] == L'/')
            path[size] = L'\\';
    ok = HttpGet(url, path, NULL, NULL)
         && Mods_Import(gameDir, path, info->type == MOD_MANAGER ? NULL : info->folder, folder);
    DeleteTree(temp);

    if (ok && folder[0]) {
        ReleaseFilePath(gameDir, folder, path, LONG_PATH);
        WritePrivateProfileStringW(L"Release", L"Tag", tag, path);
        WritePrivateProfileStringW(L"Release", L"Name", info->name, path);
    }
    return ok;
}

static void ImageText(const char *base, size_t size, const char *text, wchar_t *out, int cap)
{
    out[0] = 0;
    if (text >= base && text < base + size && memchr(text, 0, (size_t)(base + size - text)))
        Widen(text, out, cap);
}

void Mods_FreeSchema(ModSchema *schema)
{
    free(schema->settings);
    memset(schema, 0, sizeof(*schema));
}

bool Mods_LoadSchema(const wchar_t *dllPath, ModSchema *schema)
{
    HMODULE module = LoadLibraryExW(dllPath, NULL, DONT_RESOLVE_DLL_REFERENCES);
    const char *base = (const char *)module, *table;
    EfzConfigSchema header;
    wchar_t choices[9 * 32];
    size_t size;
    unsigned i;

    memset(schema, 0, sizeof(*schema));
    if (!module)
        return false;
    size = ((const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew))
               ->OptionalHeader.SizeOfImage;
    table = (const char *)GetProcAddress(module, "EFZ_ConfigSchema");
    if (table && table >= base && table + sizeof(header) <= base + size) {
        memcpy(&header, table, sizeof(header));
        table = (const char *)header.settings;
        if (header.version >= 1 && header.settingSize >= sizeof(EfzConfigSetting)
                && header.count >= 1 && header.count <= 1024
                && table >= base && table < base + size
                && (size_t)header.count * header.settingSize <= (size_t)(base + size - table))
            schema->settings = (ModSetting *)calloc(header.count, sizeof(ModSetting));
    }
    for (i = 0; schema->settings && i < header.count; ++i) {
        const EfzConfigSetting *in = (const EfzConfigSetting *)(table + (size_t)i * header.settingSize);
        ModSetting *out = &schema->settings[schema->count];
        wchar_t *choice, *bar;

        ImageText(base, size, in->section, out->section, (int)COUNT_OF(out->section));
        ImageText(base, size, in->key, out->key, (int)COUNT_OF(out->key));
        ImageText(base, size, in->label, out->label, (int)COUNT_OF(out->label));
        ImageText(base, size, in->description, out->description, (int)COUNT_OF(out->description));
        ImageText(base, size, in->fallback, out->fallback, (int)COUNT_OF(out->fallback));
        ImageText(base, size, in->choices, choices, (int)COUNT_OF(choices));
        out->type = in->type >= EFZ_SETTING_SWITCH && in->type <= EFZ_SETTING_TEXT ? in->type
                                                                                   : EFZ_SETTING_TEXT;
        out->minimum = in->minimum;
        out->maximum = in->maximum;
        out->choiceCount = 0;
        for (choice = choices; *choice && out->choiceCount < 9; choice = bar ? bar + 1 : L"") {
            bar = wcschr(choice, L'|');
            if (bar)
                *bar = 0;
            if (wcslen(choice) < 32)
                wcscpy(out->choices[out->choiceCount++], choice);
        }

        if (out->section[0] && out->key[0] && !wcspbrk(out->section, L"[]\r\n")
                && !wcspbrk(out->key, L"=[];\r\n"))
            ++schema->count;
        else
            memset(out, 0, sizeof(*out));
    }
    if (schema->settings)
        ImageText(base, size, header.iniFile, schema->iniFile, MOD_NAME_CAP);
    FreeLibrary(module);

    if (schema->count == 0 || !schema->iniFile[0] || wcspbrk(schema->iniFile, L"\\/:*?\"<>|")) {
        Mods_FreeSchema(schema);
        return false;
    }
    return true;
}
