// Tests the same guard routine used by the runtime installer, without executing game code.
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
typedef uint32_t DWORD;
typedef unsigned char BYTE;
#include "bfme2_audio_profile.h"
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<BYTE> image((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    for (const auto& g : kBfme2AudioGuards) {
        if (g.va < 0x400000 || uint64_t(g.va - 0x400000) + g.size > image.size()) return 2;
    }
    if (!bfme2AudioProfileMatches(image.data())) { puts("Baseline guard rejected"); return 1; }
    unsigned mutations = 0;
    for (const auto& g : kBfme2AudioGuards) {
        for (unsigned j = 0; j < g.size; ++j) {
            BYTE& b = image[g.va - 0x400000 + j];
            b ^= 1;
            bool rejected = !bfme2AudioProfileMatches(image.data());
            b ^= 1;
            if (!rejected) { puts("Changed code accepted"); return 1; }
            ++mutations;
        }
    }
    if (!bfme2AudioProfileMatches(image.data())) return 1;
    printf("BFME II audio guards: baseline accepted, %u single-byte mutations rejected\n", mutations);
    return 0;
}
