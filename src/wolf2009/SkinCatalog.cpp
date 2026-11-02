// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinCatalog.h"
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string Path(std::string name) {
    Need(!name.empty() && name.size() <= 1024, "Unsupported skin virtual name");
    for (char &c : name) {
        Need(static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) < 127 && c != ':' &&
                 c != '*',
             "Unsupported skin virtual-name byte");
        if (c == '\\')
            c = '/';
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    }
    Need(name[0] != '/' && name.back() != '/', "Absolute/empty skin virtual path");
    std::size_t p = 0;
    while (p < name.size()) {
        const auto e = name.find('/', p);
        const auto part = name.substr(p, e == std::string::npos ? e : e - p);
        Need(!part.empty() && part != "." && part != "..", "Malformed skin virtual path");
        if (e == std::string::npos)
            break;
        p = e + 1;
    }
    return name;
}
class Lexer {
    const std::string &source_;
    const SkinLimits &limits_;
    std::size_t at_ = 0, count_ = 0;

  public:
    Lexer(const std::string &text, const SkinLimits &limits) : source_(text), limits_(limits) {}
    std::string Next() {
        for (;;) {
            while (at_ < source_.size() && static_cast<unsigned char>(source_[at_]) <= 32) {
                Need(source_[at_] != 0, "NUL in skin source");
                ++at_;
            }
            if (at_ + 1 < source_.size() && source_[at_] == '/' && source_[at_ + 1] == '/') {
                at_ += 2;
                while (at_ < source_.size() && source_[at_] != '\n')
                    ++at_;
                continue;
            }
            if (at_ + 1 < source_.size() && source_[at_] == '/' && source_[at_ + 1] == '*') {
                at_ += 2;
                const auto end = source_.find("*/", at_);
                Need(end != std::string::npos, "Unterminated skin comment");
                at_ = end + 2;
                continue;
            }
            break;
        }
        if (at_ == source_.size())
            return "";
        Need(++count_ <= limits_.tokens, "Skin token count budget");
        const char first = source_[at_++];
        if (first == '{' || first == '}')
            return std::string(1, first);
        std::string token;
        const bool quoted = first == '"';
        if (!quoted)
            token += first;
        while (at_ < source_.size()) {
            const char c = source_[at_];
            if (quoted && c == '"') {
                ++at_;
                Need(!token.empty(), "Empty skin token");
                return token;
            }
            if (!quoted && (static_cast<unsigned char>(c) <= 32 || c == '{' || c == '}'))
                break;
            Need(static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) < 127,
                 "Unsupported skin token byte");
            Need(token.size() < limits_.tokenBytes, "Skin token byte budget");
            token += c;
            ++at_;
        }
        Need(!quoted, "Unterminated skin string");
        Need(!token.empty() && token.size() <= limits_.tokenBytes, "Skin token byte budget");
        return token;
    }
};
} // namespace
SkinCatalog SkinCatalog::Read(std::istream& input,const ZipArchive& archive,const SkinLimits& limits) {
    SkinCatalog catalog(limits);
    for (std::size_t i=0;i<archive.Entries().size();++i) {
        const auto& entry=archive.Entries()[i];
        if(entry.name.size()<5) continue;
        auto ext=entry.name.substr(entry.name.size()-5);
        for(char& c:ext) if(c>='A' && c<='Z') c=char(c+32);
        if(ext!=".skin") continue;
        Need(entry.size<=limits.sourceBytes && entry.size<=limits.totalSourceBytes-catalog.sourceBytes_,
             "Skin declarations exceed the configured byte limit");
        const auto bytes=archive.ReadEntry(input,i);
        catalog.AddSource(entry.name,std::string(bytes.begin(),bytes.end()));
    }
    return catalog;
}
void SkinCatalog::AddSource(const std::string &source, const std::string &text) {
    const auto sourceName = Path(source);
    Need(text.size() <= limits_.sourceBytes && sourceBytes_ <= limits_.totalSourceBytes &&
             text.size() <= limits_.totalSourceBytes - sourceBytes_,
         "Skin source byte budget");
    Lexer lexer(text, limits_);
    auto staged = definitions_;
    std::size_t rules = rules_;
    for (std::string token = lexer.Next(); !token.empty(); token = lexer.Next()) {
        std::string name = token;
        if (token == "skin")
            name = lexer.Next();
        name = Path(name);
        Need(lexer.Next() == "{", "Expected skin declaration body");
        Definition definition;
        definition.source = sourceName;
        for (;;) {
            const auto from = lexer.Next();
            Need(!from.empty(), "Unterminated skin declaration");
            if (from == "}")
                break;
            Need(from != "{", "Nested skin body unsupported");
            const auto to = lexer.Next();
            Need(!to.empty() && to != "{" && to != "}", "Skin remap needs two names");
            Need(rules < limits_.rules, "Skin rule budget");
            ++rules;
            if (from == "*" || to == "*" || from == "model") {
                definition.unsupported = true;
                continue;
            }
            if (!definition.remaps.insert(std::make_pair(Path(from), Path(to))).second)
                definition.unsupported = true;
        }
        auto found = staged.find(name);
        if (found != staged.end())
            found->second.unsupported = true;
        else {
            Need(staged.size() < limits_.skins, "Skin declaration budget");
            staged.insert(std::make_pair(std::move(name), std::move(definition)));
        }
    }
    definitions_.swap(staged);
    sourceBytes_ += text.size();
    rules_ = rules;
}
bool SkinCatalog::Contains(const std::string &name) const { return definitions_.count(Path(name)) != 0; }
SkinApplication SkinCatalog::Apply(const std::string &name, GeometryScene &geometry) const {
    SkinApplication result;
    result.name = Path(name);
    const auto found = definitions_.find(result.name);
    Need(found != definitions_.end(), "Required skin declaration is missing");
    Need(!found->second.unsupported,
         "Selected skin has ambiguous or unsupported wildcard/directive rules");
    std::vector<std::string> materials;
    materials.reserve(geometry.surfaces.size());
    for (const auto &surface : geometry.surfaces) {
        const auto key = Path(surface.material);
        const auto rule = found->second.remaps.find(key);
        if (rule != found->second.remaps.end()) {
            ++result.matchedSurfaces;
            if (key != rule->second) {
                ++result.changedSurfaces;
                materials.push_back(rule->second);
            } else
                materials.push_back(surface.material);
        } else
            materials.push_back(surface.material);
    }
    for (std::size_t i = 0; i < materials.size(); i++)
        geometry.surfaces[i].material.swap(materials[i]);
    return result;
}
} // namespace wolf2009
