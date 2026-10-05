// Original implementation of an exact subset of BFME II's equivalence answers.
// Reads current data every time: no cached pointers or lifetime assumptions.
#ifndef BFME2_EQUIV_CORE_H
#define BFME2_EQUIV_CORE_H
static __forceinline DWORD bfme2TemplateWord(const BYTE* p, unsigned offset) {
    DWORD word; memcpy(&word, p + offset, sizeof(word)); return word;
}
static __forceinline const BYTE* bfme2FinalTemplate(const BYTE* p) {
    DWORD next;
    while ((next = bfme2TemplateWord(p, 4)) != 0) p = (const BYTE*)(ULONG_PTR)next;
    return p;
}
// -1 delegates to the original; otherwise the result is an exact boolean.
static __forceinline int bfme2EquivDecision(const BYTE* a, const BYTE* b) {
    if (!a || !b) return 0;
    if (a == b) return 1;
    // The original compares lists on a/b, not on their final override objects.
    if (bfme2TemplateWord(a, 0x330) != bfme2TemplateWord(a, 0x334) ||
        bfme2TemplateWord(b, 0x330) != bfme2TemplateWord(b, 0x334) ||
        bfme2TemplateWord(a, 0x33C) != bfme2TemplateWord(a, 0x340) ||
        bfme2TemplateWord(b, 0x33C) != bfme2TemplateWord(b, 0x340)) return -1;
    return bfme2FinalTemplate(a) == bfme2FinalTemplate(b) ? 1 : 0;
}
#endif
