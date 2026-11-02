// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeclarationText.h"
#include <stdexcept>
namespace wolf2009 {
namespace detail {
namespace {
void Need(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
} // namespace
std::string Virtual(std::string value) {
    Need(!value.empty() && value.size() <= 4096, "Invalid voice virtual name length");
    for (char &c : value) {
        Need(static_cast<unsigned char>(c) >= 33 && static_cast<unsigned char>(c) < 127 && c != ':',
             "Invalid voice virtual name byte");
        if (c == '\\')
            c = '/';
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    }
    Need(value[0] != '/' && value.back() != '/', "Absolute/empty voice path component");
    std::size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find('/', start);
        const auto part = value.substr(start, end == std::string::npos ? end : end - start);
        Need(!part.empty() && part != "." && part != "..", "Unsafe voice virtual path component");
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return value;
}

std::vector<Token> Tokens(const std::string &text, const DeclarationLimits &limits) {
    Need(text.size() <= limits.sourceBytes, "Declaration source byte budget");
    std::vector<Token> result;
    std::size_t p = 0;
    while (p < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[p]);
        if (c <= 32) {
            Need(c == ' ' || c == '\t' || c == '\r' || c == '\n',
                 "Invalid declaration source control byte");
            ++p;
            continue;
        }
        if (text.compare(p, 2, "//") == 0) {
            while (p < text.size() && text[p] != '\n')
                ++p;
            continue;
        }
        if (text.compare(p, 2, "/*") == 0) {
            const auto end = text.find("*/", p + 2);
            Need(end != std::string::npos, "Unterminated declaration source comment");
            p = end + 2;
            continue;
        }
        Need(result.size() < limits.tokens, "Declaration source token budget");
        Token token;
        if (text[p] == '{' || text[p] == '}') {
            token.brace = text[p++];
        } else if (text[p] == '"') {
            ++p;
            bool closed = false;
            while (p < text.size()) {
                char ch = text[p++];
                if (ch == '"') {
                    closed = true;
                    break;
                }
                if (ch == '\\' && p < text.size() && (text[p] == '"' || text[p] == '\\'))
                    ch = text[p++];
                Need(static_cast<unsigned char>(ch) >= 32 && ch != 127 &&
                         token.value.size() < limits.tokenBytes,
                     "Invalid/excessive quoted token");
                token.value += ch;
            }
            Need(closed, "Unterminated declaration source quote");
        } else {
            while (p < text.size() && static_cast<unsigned char>(text[p]) > 32 && text[p] != '{' &&
                   text[p] != '}') {
                Need(text[p] != '"' && text[p] != 127 && token.value.size() < limits.tokenBytes,
                     "Invalid/excessive bare token");
                token.value += text[p++];
            }
        }
        result.push_back(token);
    }
    return result;
}

std::vector<Token> Selected(const std::string &text, const char *type, const std::string &name,
                            const DeclarationLimits &limits) {
    const auto tokens = Tokens(text, limits);
    std::vector<Token> selected;
    bool found = false;
    std::size_t p = 0, declarations = 0;
    while (p < tokens.size()) {
        Need(declarations < limits.declarations, "Declaration count budget");
        ++declarations;
        Need(tokens.size() - p >= 3 && !tokens[p].brace && !tokens[p + 1].brace &&
                 tokens[p + 2].brace == '{',
             "Malformed declaration header");
        const bool matches = tokens[p].value == type && Virtual(tokens[p + 1].value) == name;
        const auto begin = p + 3;
        p = begin;
        std::size_t depth = 1;
        Need(depth <= limits.nesting, "Declaration nesting budget");
        while (p < tokens.size() && depth) {
            if (tokens[p].brace == '{') {
                ++depth;
                Need(depth <= limits.nesting, "Declaration nesting budget");
            } else if (tokens[p].brace == '}') {
                --depth;
            }
            ++p;
        }
        Need(depth == 0, "Unclosed declaration");
        if (matches) {
            Need(!found, "Ambiguous selected declaration");
            found = true;
            selected.assign(tokens.begin() + begin, tokens.begin() + p - 1);
        }
    }
    Need(found, "Missing selected declaration");
    return selected;
}

std::string ZipText(std::istream &input, const ZipArchive &zip, const std::string &name,
                    const DeclarationLimits &limits) {
    std::size_t index = 0, matches = 0;
    for (std::size_t i = 0; i < zip.Entries().size(); ++i) {
        // Unrelated ZIP directory entries need not be virtual resource names.
        auto candidate = zip.Entries()[i].name;
        for (char &c : candidate) {
            if (c == '\\')
                c = '/';
            if (c >= 'A' && c <= 'Z')
                c = char(c + 32);
        }
        if (candidate == name) {
            index = i;
            ++matches;
        }
    }
    Need(matches == 1, "Missing/ambiguous declaration ZIP source member");
    Need(zip.Entries()[index].size <= limits.sourceBytes, "Voice ZIP source byte budget");
    const auto bytes = zip.ReadEntry(input, index);
    return std::string(bytes.begin(), bytes.end());
}
} // namespace detail
} // namespace wolf2009
