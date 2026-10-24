#ifndef EFZ_CONFIG_INI_H
#define EFZ_CONFIG_INI_H

#include <windows.h>
#include <stdbool.h>
#include <wchar.h>

enum IniEncoding {
    INI_ENC_UTF16LE = 0,
    INI_ENC_UTF8_BOM,
    INI_ENC_UTF8_NOBOM,
    INI_ENC_ANSI,
};

enum IniTerminator {
    INI_TERM_NONE = 0,
    INI_TERM_LF,
    INI_TERM_CR,
    INI_TERM_CRLF,
};

typedef struct IniLine {
    wchar_t *text;
    int section;
    unsigned char term;
} IniLine;

typedef struct IniSection {
    wchar_t name[64];
    int first_line;
} IniSection;

typedef struct IniFile {
    wchar_t path[MAX_PATH];
    IniLine *lines;
    int line_count, line_cap;
    IniSection sections[32];
    int section_count;
    enum IniEncoding encoding;
    bool crlf;
} IniFile;

bool Ini_Load(IniFile *ini, const wchar_t *path);
void Ini_Free(IniFile *ini);

int Ini_FindSection(const IniFile *ini, const wchar_t *name);

bool Ini_ParseLine(const IniFile *ini, int line, wchar_t *key, size_t key_cap,
                   wchar_t *value, size_t value_cap);

int Ini_FindKey(const IniFile *ini, const wchar_t *section, const wchar_t *key);

bool Ini_SetValue(IniFile *ini, const wchar_t *section, const wchar_t *key,
                  const wchar_t *value);

bool Ini_ReplaceLine(IniFile *ini, int line, const wchar_t *text);

bool Ini_InsertKey(IniFile *ini, const wchar_t *section, const wchar_t *key,
                   const wchar_t *value, bool at_top);

bool Ini_Save(const IniFile *ini);

#endif
