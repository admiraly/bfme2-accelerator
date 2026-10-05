// Test-only compatibility spellings for old CRTs on current Windows NLS data.
#pragma once
#include <locale.h>
#include <cstring>
typedef char* (__cdecl* tCrtSetLocale)(int, const char*);
static bool crtTestLocale(tCrtSetLocale setLocale, const char* locale) {
    if (strcmp(locale, "Turkish")) return setLocale(LC_CTYPE, locale) != NULL;
    // Windows renamed Turkey to Turkiye/Tuerkiye in newer locale metadata;
    // prefer the language-only form, then old/new explicit country spellings.
    const char* names[] = {"Turkish", "turkish", "tur", "Turkish_Turkey.1254",
                           "Turkish_T\xFC" "rkiye.1254", "Turkish_Turkiye.1254"};
    for (const char* name : names) if (setLocale(LC_CTYPE, name)) return true;
    return false;
}
