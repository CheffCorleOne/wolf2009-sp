// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_DECLARATION_TEXT_H
#define WOLF2009_DECLARATION_TEXT_H
#include "ZipArchive.h"
namespace wolf2009 {
struct DeclarationLimits {
    std::size_t sourceBytes = 4u * 1024u * 1024u, tokens = 500000, tokenBytes = 8192,
                declarations = 20000, nesting = 64;
};
namespace detail {
struct Token {
    std::string value;
    char brace = 0;
};
std::string Virtual(std::string);
std::vector<Token> Selected(const std::string &text, const char *type, const std::string &name,
                            const DeclarationLimits &limits = {});
std::string ZipText(std::istream &input, const ZipArchive &archive, const std::string &name,
                    const DeclarationLimits &limits = {});
} // namespace detail
} // namespace wolf2009
#endif
