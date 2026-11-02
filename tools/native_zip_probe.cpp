// SPDX-License-Identifier: GPL-3.0-or-later
// Integration probe linked to the actual upstream Unzip.cpp, both backends.
#include <cstdio>
#include <cstring>
typedef unsigned char byte;
#include "framework/Unzip.h"
#include "framework/Wolf2009Zip.h"

int main(int argc, char** argv) {
    bool reject = argc > 1 && std::strcmp(argv[1], "--reject") == 0;
    const int first = reject ? 2 : 1;
    if (argc <= first) return 2;
    for (int a = first; a < argc; ++a) {
        unzFile file = unzOpen(argv[a]);
        if (reject) { if (file) { unzClose(file); return 1; } continue; }
        if (!file) { std::fprintf(stderr, "Cannot open archive: %s\n", argv[a]); return 1; }
#ifdef _MINIZ
        d3_unz_global_info info;
        int result = d3_unzGetGlobalInfo(file, &info);
#else
        unz_global_info info;
        int result = unzGetGlobalInfo(file, &info);
#endif
        unsigned long long files = 0, bytes = 0;
        bool ok = result == UNZ_OK && info.number_entry <= 100000;
        if (ok && info.number_entry) result = unzGoToFirstFile(file);
        for (unsigned long long i = 0; ok && i < info.number_entry; ++i) {
#ifdef _MINIZ
            d3_unz_file_info member;
            ok = result == UNZ_OK && d3_unzGetCurrentFileInfo(file, &member, NULL, 0, NULL, 0, NULL, 0) == UNZ_OK;
#else
            unz_file_info member;
            ok = result == UNZ_OK && unzGetCurrentFileInfo(file, &member, NULL, 0, NULL, 0, NULL, 0) == UNZ_OK;
#endif
            if (!ok || member.uncompressed_size > 256ULL * 1024 * 1024) { ok = false; break; }
            if (unzOpenCurrentFile(file) != UNZ_OK) { ok = false; break; }
            unsigned char data[65536];
            unsigned long long size = 0;
            int read;
            while ((read = unzReadCurrentFile(file, data, sizeof(data))) > 0) {
                size += read;
                if (size > member.uncompressed_size) { ok = false; break; }
            }
            ok = ok && read == 0 && size == member.uncompressed_size;
            const int closeResult = unzCloseCurrentFile(file); // upstream CRC32 check
            ok = ok && closeResult == UNZ_OK;
            ++files; bytes += size;
            if (i + 1 < info.number_entry) result = unzGoToNextFile(file);
        }
        unzClose(file);
        if (!ok) { std::fprintf(stderr, "Member validation failed: %s\n", argv[a]); return 1; }
        std::printf("validated files=%llu bytes=%llu archive=%s\n", files, bytes, argv[a]);
    }
    return 0;
}
