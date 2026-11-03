// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_COMPILED_DECLARATIONS_H
#define WOLF2009_COMPILED_DECLARATIONS_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace wolf2009 {
// Our explicit admission profile, not an embedded source version/magic.
enum class CompiledDeclFormat { Unspecified, TwoSectionLfTextV1 };
enum class CompiledDeclFailure { InvalidData, Unsupported, LimitExceeded };
class CompiledDeclError : public std::runtime_error {
    CompiledDeclFailure category_;
    std::size_t offset_;
  public:
    CompiledDeclError(CompiledDeclFailure category,std::size_t offset,const char* reason)
        :std::runtime_error(reason),category_(category),offset_(offset){}
    CompiledDeclFailure Category() const { return category_; }
    std::size_t Offset() const { return offset_; }
};
// Half-open source-byte spans. Text metadata spans exclude their required LF;
// pool countSpan covers its four binary LE bytes, with no text delimiter.
struct CompiledDeclSpan { std::size_t begin=0,end=0; };
struct CompiledDeclWordPool {
    CompiledDeclSpan countSpan,dataSpan;
    std::vector<std::uint32_t> words; // Exact LE word bits, no inferred type.
};
struct CompiledDeclRecord {
    std::uint32_t rawId=0; // Not a universal type, line number or global index.
    CompiledDeclSpan idSpan,lengthSpan,textSpan;
    std::vector<unsigned char> text; // Raw length-delimited bytes, including NUL.
};
struct CompiledDeclGroup {
    std::string sourceFile; // Source metadata label; never opened/canonicalized.
    std::uint32_t defaultTypeId=0; // Reader's default; explicit payload type may override.
    CompiledDeclSpan sourceFileSpan,defaultTypeSpan,countSpan;
    std::vector<CompiledDeclRecord> records;
};
struct CompiledDeclSection {
    CompiledDeclSpan countSpan;
    std::vector<CompiledDeclGroup> groups;
};
struct CompiledDeclLimits {
    std::size_t sourceBytes=64u<<20;
    std::size_t poolWords=1000000,groups=20000,records=100000; // Cumulative across both.
    std::size_t textBytes=4u<<20; // Per record; zero-length payloads are legal.
    std::size_t metadataBytes=4096; // Per line, excluding LF.
    // Conservative retained-data charge for owning strings/vectors/objects;
    // not an exact allocator or peak-RSS bound. Input bytes belong to caller.
    std::size_t retainedBytes=128u<<20;
};
struct CompiledDeclarations {
    std::array<CompiledDeclWordPool,2> pools;
    std::array<CompiledDeclSection,2> sections;
    std::size_t sourceBytes=0;
    std::uint64_t sourceFingerprint=0; // Computed FNV-1a, local identity only.
    // Transport only: two LE count/word arrays followed by two text sections.
    // Canonical uint32 decimal LF metadata; nonempty printable-ASCII labels.
    // Payloads are exact raw byte slices, not parsed/qualified declarations.
    // Bounds/count/retained budgets are checked before allocation/iteration;
    // no source pointer escapes, exact EOF is required, failure is transactional.
    static CompiledDeclarations Read(const std::vector<unsigned char>& bytes,
        CompiledDeclFormat format=CompiledDeclFormat::Unspecified,
        const CompiledDeclLimits& limits=CompiledDeclLimits());
};
} // namespace wolf2009
#endif
