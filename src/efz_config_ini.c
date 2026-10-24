#include "efz_config_ini.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static bool is_space(wchar_t ch)
{
    return ch == L' ' || ch == L'\t';
}

static void trim_span(const wchar_t **start, const wchar_t **end)
{
    while (*start < *end && is_space(**start))
        ++*start;
    while (*end > *start && is_space((*end)[-1]))
        --*end;
}

static bool split_line(const wchar_t *line, const wchar_t **key, const wchar_t **key_end,
                       const wchar_t **value, const wchar_t **value_end)
{
    const wchar_t *eq;

    while (is_space(*line))
        ++line;
    if (*line == L';' || *line == L'[' || *line == 0)
        return false;
    eq = wcschr(line, L'=');
    if (!eq)
        return false;
    *key = line;
    *key_end = eq;
    trim_span(key, key_end);
    if (*key == *key_end)
        return false;
    *value = eq + 1;
    *value_end = *value + wcslen(*value);
    trim_span(value, value_end);
    return true;
}

static void copy_span(wchar_t *out, size_t cap, const wchar_t *start, const wchar_t *end)
{
    size_t len = (size_t)(end - start);
    if (cap == 0)
        return;
    if (len >= cap)
        len = cap - 1;
    memcpy(out, start, len * sizeof(wchar_t));
    out[len] = 0;
}

static wchar_t *dup_span(const wchar_t *start, size_t len)
{
    wchar_t *copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy) {
        memcpy(copy, start, len * sizeof(wchar_t));
        copy[len] = 0;
    }
    return copy;
}

static bool insert_line(IniFile *ini, int index, const wchar_t *text, size_t len, int section)
{
    wchar_t *copy;
    unsigned char term = ini->crlf ? INI_TERM_CRLF : INI_TERM_LF;
    int i;

    if (ini->line_count == ini->line_cap) {
        int cap = ini->line_cap ? ini->line_cap * 2 : 256;
        IniLine *grown = (IniLine *)realloc(ini->lines, (size_t)cap * sizeof(IniLine));
        if (!grown)
            return false;
        ini->lines = grown;
        ini->line_cap = cap;
    }
    copy = dup_span(text, len);
    if (!copy)
        return false;

    memmove(ini->lines + index + 1, ini->lines + index,
            (size_t)(ini->line_count - index) * sizeof(IniLine));
    ini->lines[index].text = copy;
    ini->lines[index].section = section;
    ini->lines[index].term = term;
    if (index == ini->line_count && index > 0
            && ini->lines[index - 1].term == INI_TERM_NONE) {
        ini->lines[index - 1].term = term;
        ini->lines[index].term = INI_TERM_NONE;
    }
    ++ini->line_count;
    for (i = 0; i < ini->section_count; ++i)
        if (ini->sections[i].first_line >= index)
            ++ini->sections[i].first_line;
    return true;
}

static bool read_all_bytes(const wchar_t *path, uint8_t **out, size_t *size_out)
{
    HANDLE file;
    DWORD size, read_bytes = 0;
    uint8_t *buffer;
    bool ok;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(file);
        return false;
    }
    buffer = (uint8_t *)malloc(size);
    ok = buffer && ReadFile(file, buffer, size, &read_bytes, NULL) && read_bytes == size;
    CloseHandle(file);
    if (!ok) {
        free(buffer);
        return false;
    }
    *out = buffer;
    *size_out = size;
    return true;
}

static bool looks_like_strict_utf8(const uint8_t *bytes, size_t size)
{
    size_t i = 0;
    while (i < size) {
        uint8_t b = bytes[i];
        int extra;
        if (b < 0x80) {
            ++i;
            continue;
        }
        if ((b & 0xE0) == 0xC0)
            extra = 1;
        else if ((b & 0xF0) == 0xE0)
            extra = 2;
        else if ((b & 0xF8) == 0xF0)
            extra = 3;
        else
            return false;
        if (i + (size_t)extra >= size)
            return false;
        while (extra--) {
            if ((bytes[++i] & 0xC0) != 0x80)
                return false;
        }
        ++i;
    }
    return true;
}

static wchar_t *decode_to_wide(const uint8_t *bytes, size_t size, enum IniEncoding enc,
                               size_t *chars_out)
{
    UINT code_page = enc == INI_ENC_ANSI ? CP_ACP : CP_UTF8;
    wchar_t *wide;
    int needed;

    if (enc == INI_ENC_UTF16LE) {
        *chars_out = size / 2;
        return dup_span((const wchar_t *)bytes, size / 2);
    }
    needed = MultiByteToWideChar(code_page, 0, (const char *)bytes, (int)size, NULL, 0);
    if (needed <= 0)
        return NULL;
    wide = (wchar_t *)malloc(((size_t)needed + 1) * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(code_page, 0, (const char *)bytes, (int)size, wide, needed);
    wide[needed] = 0;
    *chars_out = (size_t)needed;
    return wide;
}

static void note_section(IniFile *ini, int line)
{
    const wchar_t *start = ini->lines[line].text, *end, *close;
    IniSection *sec;

    while (is_space(*start))
        ++start;
    if (*start != L'[' || ini->section_count >= (int)COUNT_OF(ini->sections))
        return;
    ++start;
    close = wcschr(start, L']');
    if (!close)
        return;
    end = close;
    trim_span(&start, &end);
    if (start == end || (size_t)(end - start) >= COUNT_OF(sec->name))
        return;
    sec = &ini->sections[ini->section_count];
    copy_span(sec->name, COUNT_OF(sec->name), start, end);
    sec->first_line = line;
    ini->lines[line].section = ini->section_count++;
}

bool Ini_Load(IniFile *ini, const wchar_t *path)
{
    uint8_t *bytes = NULL;
    size_t size = 0, chars = 0, i = 0, skip = 0;
    wchar_t *wide;
    bool ok = true;

    memset(ini, 0, sizeof(*ini));
    wcsncpy(ini->path, path, MAX_PATH - 1);

    if (!read_all_bytes(path, &bytes, &size))
        return false;

    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        ini->encoding = INI_ENC_UTF16LE;
        skip = 2;
    } else if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        ini->encoding = INI_ENC_UTF8_BOM;
        skip = 3;
    } else if (looks_like_strict_utf8(bytes, size)) {
        ini->encoding = INI_ENC_UTF8_NOBOM;
    } else {
        ini->encoding = INI_ENC_ANSI;
    }

    wide = decode_to_wide(bytes + skip, size - skip, ini->encoding, &chars);
    free(bytes);
    if (!wide)
        return false;

    ini->crlf = true;
    for (i = 0; i < chars; ++i) {
        if (wide[i] == L'\n') {
            ini->crlf = i > 0 && wide[i - 1] == L'\r';
            break;
        }
    }

    i = 0;
    while (ok && i < chars) {
        size_t start = i;
        unsigned char term = INI_TERM_NONE;
        int line = ini->line_count;

        while (i < chars && wide[i] != L'\r' && wide[i] != L'\n')
            ++i;
        ok = insert_line(ini, line, wide + start, i - start,
                         ini->section_count - 1);
        if (!ok)
            break;
        if (i < chars) {
            if (wide[i] == L'\r' && i + 1 < chars && wide[i + 1] == L'\n') {
                term = INI_TERM_CRLF;
                i += 2;
            } else {
                term = wide[i] == L'\r' ? INI_TERM_CR : INI_TERM_LF;
                ++i;
            }
        }
        ini->lines[line].term = term;
        note_section(ini, line);
    }

    free(wide);
    if (!ok)
        Ini_Free(ini);
    return ok;
}

void Ini_Free(IniFile *ini)
{
    int i;
    for (i = 0; i < ini->line_count; ++i)
        free(ini->lines[i].text);
    free(ini->lines);
    ini->lines = NULL;
    ini->line_count = ini->line_cap = ini->section_count = 0;
}

int Ini_FindSection(const IniFile *ini, const wchar_t *name)
{
    int i;
    for (i = 0; i < ini->section_count; ++i)
        if (_wcsicmp(ini->sections[i].name, name) == 0)
            return i;
    return -1;
}

bool Ini_ParseLine(const IniFile *ini, int line, wchar_t *key, size_t key_cap,
                   wchar_t *value, size_t value_cap)
{
    const wchar_t *k, *ke, *v, *ve;

    if (line < 0 || line >= ini->line_count
            || !split_line(ini->lines[line].text, &k, &ke, &v, &ve))
        return false;
    copy_span(key, key_cap, k, ke);
    copy_span(value, value_cap, v, ve);
    return true;
}

int Ini_FindKey(const IniFile *ini, const wchar_t *section, const wchar_t *key)
{
    int section_index = Ini_FindSection(ini, section);
    size_t key_len = wcslen(key);
    int i;

    if (section_index < 0)
        return -1;
    for (i = ini->sections[section_index].first_line + 1;
            i < ini->line_count && ini->lines[i].section == section_index; ++i) {
        const wchar_t *k, *ke, *v, *ve;
        if (split_line(ini->lines[i].text, &k, &ke, &v, &ve)
                && (size_t)(ke - k) == key_len && _wcsnicmp(k, key, key_len) == 0)
            return i;
    }
    return -1;
}

bool Ini_SetValue(IniFile *ini, const wchar_t *section, const wchar_t *key,
                  const wchar_t *value)
{
    int line = Ini_FindKey(ini, section, key);
    const wchar_t *k, *ke, *v, *ve;
    wchar_t *old, *rebuilt;
    size_t head, tail, value_len = wcslen(value);

    if (line < 0)
        return false;
    old = ini->lines[line].text;
    split_line(old, &k, &ke, &v, &ve);
    head = (size_t)(v - old);
    tail = wcslen(ve);
    rebuilt = (wchar_t *)malloc((head + value_len + tail + 1) * sizeof(wchar_t));
    if (!rebuilt)
        return false;
    memcpy(rebuilt, old, head * sizeof(wchar_t));
    memcpy(rebuilt + head, value, value_len * sizeof(wchar_t));
    memcpy(rebuilt + head + value_len, ve, (tail + 1) * sizeof(wchar_t));
    free(old);
    ini->lines[line].text = rebuilt;
    return true;
}

bool Ini_ReplaceLine(IniFile *ini, int line, const wchar_t *text)
{
    wchar_t *copy = dup_span(text, wcslen(text));
    if (!copy)
        return false;
    free(ini->lines[line].text);
    ini->lines[line].text = copy;
    return true;
}

bool Ini_InsertKey(IniFile *ini, const wchar_t *section, const wchar_t *key,
                   const wchar_t *value, bool at_top)
{
    int section_index = Ini_FindSection(ini, section);
    size_t len = wcslen(key) + 1 + wcslen(value);
    wchar_t *text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    int at, i;
    bool ok;

    if (!text)
        return false;
    wcscpy(text, key);
    wcscat(text, L"=");
    wcscat(text, value);

    if (section_index < 0) {
        wchar_t header[COUNT_OF(ini->sections[0].name) + 2];
        IniSection *sec = &ini->sections[ini->section_count];

        ok = ini->section_count < (int)COUNT_OF(ini->sections)
             && wcslen(section) < COUNT_OF(sec->name);
        if (ok && ini->line_count > 0)
            ok = insert_line(ini, ini->line_count, L"", 0, ini->section_count - 1);
        if (ok) {
            wcscpy(header, L"[");
            wcscat(header, section);
            wcscat(header, L"]");
            ok = insert_line(ini, ini->line_count, header, wcslen(header),
                             ini->section_count);
        }
        if (ok) {
            wcscpy(sec->name, section);
            sec->first_line = ini->line_count - 1;
            section_index = ini->section_count++;
            ok = insert_line(ini, ini->line_count, text, len, section_index);
        }
        free(text);
        return ok;
    }

    at = ini->sections[section_index].first_line + 1;
    if (!at_top) {
        for (i = at; i < ini->line_count && ini->lines[i].section == section_index; ++i) {
            const wchar_t *k, *ke, *v, *ve;
            if (split_line(ini->lines[i].text, &k, &ke, &v, &ve))
                at = i + 1;
        }
    }
    ok = insert_line(ini, at, text, len, section_index);
    free(text);
    return ok;
}

bool Ini_Save(const IniFile *ini)
{
    static const wchar_t *const kTerms[4] = { L"", L"\n", L"\r", L"\r\n" };
    size_t total = 0, pos = 0, byte_count = 0, bom = 0;
    wchar_t *flat;
    wchar_t tmp_path[MAX_PATH + 8];
    uint8_t *bytes = NULL;
    HANDLE file;
    DWORD written = 0;
    bool ok = false;
    int i;

    if (wcslen(ini->path) + 5 > MAX_PATH)
        return false;
    for (i = 0; i < ini->line_count; ++i)
        total += wcslen(ini->lines[i].text) + wcslen(kTerms[ini->lines[i].term & 3]);
    if (total == 0)
        return false;

    flat = (wchar_t *)malloc((total + 1) * sizeof(wchar_t));
    if (!flat)
        return false;
    for (i = 0; i < ini->line_count; ++i) {
        const wchar_t *term = kTerms[ini->lines[i].term & 3];
        size_t len = wcslen(ini->lines[i].text), term_len = wcslen(term);
        memcpy(flat + pos, ini->lines[i].text, len * sizeof(wchar_t));
        memcpy(flat + pos + len, term, term_len * sizeof(wchar_t));
        pos += len + term_len;
    }

    if (ini->encoding == INI_ENC_UTF16LE) {
        bom = 2;
        byte_count = bom + total * 2;
        bytes = (uint8_t *)malloc(byte_count);
        if (bytes) {
            bytes[0] = 0xFF;
            bytes[1] = 0xFE;
            memcpy(bytes + bom, flat, total * 2);
        }
    } else {
        UINT cp = ini->encoding == INI_ENC_ANSI ? CP_ACP : CP_UTF8;
        int needed = WideCharToMultiByte(cp, 0, flat, (int)total, NULL, 0, NULL, NULL);
        bom = ini->encoding == INI_ENC_UTF8_BOM ? 3 : 0;
        if (needed > 0) {
            byte_count = bom + (size_t)needed;
            bytes = (uint8_t *)malloc(byte_count);
            if (bytes) {
                memcpy(bytes, "\xEF\xBB\xBF", bom);
                WideCharToMultiByte(cp, 0, flat, (int)total,
                                    (char *)bytes + bom, needed, NULL, NULL);
            }
        }
    }
    free(flat);
    if (!bytes)
        return false;

    wcscpy(tmp_path, ini->path);
    wcscat(tmp_path, L".tmp");
    file = CreateFileW(tmp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        ok = WriteFile(file, bytes, (DWORD)byte_count, &written, NULL)
             && written == byte_count;
        CloseHandle(file);
        ok = ok && MoveFileExW(tmp_path, ini->path, MOVEFILE_REPLACE_EXISTING);
        if (!ok)
            DeleteFileW(tmp_path);
    }
    free(bytes);
    return ok;
}
