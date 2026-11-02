// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENTITIES_H
#define WOLF2009_ENTITIES_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
struct EntityLimits {
    std::size_t sourceBytes = 64u * 1024u * 1024u;
    std::size_t entities = 200000;
    std::size_t properties = 2000000;
    std::size_t dictionaryEntries = 500000;
    std::size_t dictionaryBytes = 32u * 1024u * 1024u;
    std::size_t numericValues = 8000000;
    std::size_t decodedStringBytes = 64u * 1024u * 1024u;
    std::size_t decodedNumericValues = 16000000;
    std::size_t components = 64;
};

struct EntityProperty {
    enum Kind { Text, Floats, Integer } kind = Text;
    std::string key, text;
    std::vector<float> numbers;
    std::int32_t integer = 0;
    // Preserve observed on-disk coordinates. Float literals and float arrays
    // both expose numbers, while sourceType distinguishes codes 3 and 1.
    std::uint32_t sourceType = 0, sourceIndex = 0;
};
struct EntityRecord {
    std::vector<EntityProperty> properties;
    // ASCII case-insensitive key lookup; original bytes/order are preserved.
    const EntityProperty* Find(const std::string& key) const;
};
struct EntityDocument {
    std::vector<EntityRecord> entities;
    std::size_t propertyCount = 0;
};

// Owns all decoded values. Supports the observed binary Version 4 layout and
// value types 0 (string), 1 (float array), 3 (float bits), 4 (signed integer).
// Nonempty int pools and unobserved type codes are explicitly unsupported.
EntityDocument DecodeEntities4(const std::vector<unsigned char>& data,
                               const EntityLimits& limits = EntityLimits());

struct EntityPlacement {
    std::string name, classname, model;
    float origin[3] = {0, 0, 0};
    // Stored order, intentionally not transposed or applied to geometry.
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float angle = 0;
    bool hasOrigin = false, hasRotation = false, hasAngle = false;
};
// Reads explicitly stored placement fields only. It does not resolve entity
// definitions, apply angle conventions, animate, or decide spawn expressions.
EntityPlacement ReadEntityPlacement(const EntityRecord& entity);
int RunEntityTests();
}
#endif
