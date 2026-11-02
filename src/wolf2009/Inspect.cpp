// SPDX-License-Identifier: GPL-3.0-or-later
#include "Archive.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
std::string QuotedName(const std::string& name) {
    // Prevent filenames from injecting terminal control sequences.
    const char* hex = "0123456789abcdef";
    std::string escaped = "\"";
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == '"' || c == '\\') { escaped += '\\'; escaped += char(c); }
        else if (c >= 32 && c < 127) escaped += char(c);
        else { escaped += "\\x"; escaped += hex[c >> 4]; escaped += hex[c & 15]; }
    }
    return escaped + '"';
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") return wolf2009::RunSelfTests();
    if (argc < 2 || std::string(argv[1]) == "--help") {
        std::cout << "wolf2009-inspect --self-test\n"
                     "wolf2009-inspect ARCHIVE.spk [ARCHIVE.mpk ...]\n"
                     "Read-only metadata and zlib/table/payload validation. No extraction.\n";
        return argc < 2 ? 2 : 0;
    }
    bool failed = false;
    for (int arg = 1; arg < argc; ++arg) {
        try {
            std::ifstream input(argv[arg], std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open input file");
            const wolf2009::Archive archive = wolf2009::Archive::Scan(input);
            std::cout << "archive " << QuotedName(argv[arg]) << " chunks=" << archive.Chunks().size() << '\n';
            for (std::size_t c = 0; c < archive.Chunks().size(); ++c) {
                const wolf2009::Chunk& chunk = archive.Chunks()[c];
                std::cout << "chunk " << c << " type=" << QuotedName(chunk.type)
                          << " offset=" << chunk.headerOffset
                          << " compressed=" << chunk.compressedSize
                          << " inflated=" << chunk.inflatedSize
                          << " entries=" << chunk.entries.size() << '\n';
                for (std::size_t e = 0; e < chunk.entries.size(); ++e)
                    std::cout << "  entry " << e << " bytes=" << chunk.entries[e].size
                              << " payload_offset=" << chunk.entries[e].payloadOffset
                              << " name=" << QuotedName(chunk.entries[e].name) << '\n';
            }
        } catch (const std::exception& error) {
            std::cerr << "error " << QuotedName(argv[arg]) << ": " << error.what() << '\n';
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
