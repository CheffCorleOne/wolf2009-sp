// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SKIN_CATALOG_H
#define WOLF2009_SKIN_CATALOG_H
#include "Geometry.h"
#include "ZipArchive.h"
#include <map>
namespace wolf2009 {
struct SkinLimits {
    std::size_t sourceBytes = 4u * 1024u * 1024u, totalSourceBytes = 32u * 1024u * 1024u;
    std::size_t skins = 20000, rules = 100000, tokens = 500000, tokenBytes = 1024;
};
struct SkinApplication {
    std::string name;
    std::size_t matchedSurfaces = 0, changedSurfaces = 0;
};
class SkinCatalog {
  public:
    explicit SkinCatalog(const SkinLimits &limits = SkinLimits()) : limits_(limits) {}
    static SkinCatalog Read(std::istream& input, const ZipArchive& archive,
                            const SkinLimits& limits = SkinLimits());
    // Quoted/bare virtual names, comments and explicit from/to pairs. Whole
    // sources are parsed transactionally. Duplicate declarations/rules and
    // wildcard/model directives are retained as unsupported, never guessed;
    // selecting such a skin throws. No host paths, shader visibility, animation
    // or implicit/default skin generation are evaluated.
    void AddSource(const std::string &virtualSource, const std::string &text);
    std::size_t SkinCount() const { return definitions_.size(); }
    bool Contains(const std::string &name) const;
    // Changes material names only; vertex/index storage remains untouched.
    // Missing/ambiguous/unsupported definitions leave geometry unchanged.
    SkinApplication Apply(const std::string &name, GeometryScene &geometry) const;

  private:
    struct Definition {
        std::string source;
        std::map<std::string, std::string> remaps;
        bool unsupported = false;
    };
    SkinLimits limits_;
    std::map<std::string, Definition> definitions_;
    std::size_t sourceBytes_ = 0, rules_ = 0;
};
int RunSkinCatalogTests();
} // namespace wolf2009
#endif
