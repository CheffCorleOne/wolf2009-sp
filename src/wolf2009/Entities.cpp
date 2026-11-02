// SPDX-License-Identifier: GPL-3.0-or-later
#include "Entities.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 { namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Fold(std::string value) {
    for (std::size_t i = 0; i < value.size(); ++i)
        if (value[i] >= 'A' && value[i] <= 'Z') value[i] = char(value[i] + ('a' - 'A'));
    return value;
}
float FloatBits(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "A 32-bit float host is required");
    static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 floats are required");
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    Check(std::isfinite(value), "Non-finite entity float");
    return value;
}
std::int32_t SignedBits(std::uint32_t bits) {
    // The exact two's-complement value, without implementation-defined casts.
    if (bits <= 0x7fffffffU) return static_cast<std::int32_t>(bits);
    return -1 - static_cast<std::int32_t>(0xffffffffU - bits);
}
class Cursor {
    const std::vector<unsigned char>& data_;
    std::size_t pos_ = 0;
public:
    explicit Cursor(const std::vector<unsigned char>& data) : data_(data) {}
    std::size_t Position() const { return pos_; }
    std::size_t Remaining() const { return data_.size() - pos_; }
    std::uint32_t U32() {
        Check(Remaining() >= 4, "Truncated entity integer");
        const unsigned char* p = data_.data() + pos_; pos_ += 4;
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
               (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    }
    std::vector<std::string> Line() {
        Check(pos_ < data_.size() && pos_ < 4096, "Missing or excessive entity header");
        std::vector<std::string> words;
        std::string word;
        bool newline = false;
        while (pos_ < data_.size() && pos_ < 4096) {
            const unsigned char c = data_[pos_++];
            if (c == '\n') { newline = true; break; }
            Check(c == '\t' || c == '\r' || (c >= 32 && c <= 126), "Invalid entity header character");
            if (c == ' ' || c == '\t' || c == '\r') {
                if (!word.empty()) { words.push_back(word); word.clear(); }
            } else word += char(c);
        }
        Check(newline, "Truncated entity header line");
        if (!word.empty()) words.push_back(word);
        return words;
    }
    std::vector<std::string> Strings(std::size_t count, std::size_t bytes, bool keys) {
        Check(bytes <= Remaining() && count <= bytes, "Entity string table exceeds source");
        const std::size_t end = pos_ + bytes;
        std::vector<std::string> strings; strings.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            Check(pos_ < end, "Missing entity string");
            const unsigned char* zero = static_cast<const unsigned char*>(
                std::memchr(data_.data() + pos_, 0, end - pos_));
            Check(zero != NULL, "Unterminated entity table string");
            const std::size_t length = std::size_t(zero - data_.data()) - pos_;
            Check(!keys || length != 0, "Empty entity property key");
            strings.push_back(std::string(reinterpret_cast<const char*>(data_.data() + pos_), length));
            pos_ += length + 1;
        }
        Check(pos_ == end, "Entity string byte/count mismatch");
        const std::size_t padding = (4 - (pos_ & 3)) & 3;
        Check(padding <= Remaining(), "Truncated entity table padding");
        for (std::size_t i = 0; i < padding; ++i)
            Check(data_[pos_++] == 0, "Nonzero entity table padding");
        return strings;
    }
    void End() {
        // The observed writer adds LF. Accept a missing terminator or CRLF as
        // well, but do not silently ignore unexplained binary suffixes.
        if (Remaining() == 1 && data_[pos_] == '\n') ++pos_;
        else if (Remaining() == 2 && data_[pos_] == '\r' && data_[pos_ + 1] == '\n') pos_ += 2;
        Check(Remaining() == 0, "Unexpected bytes after entity records");
    }
};
std::size_t Count(const std::string& token) {
    Check(!token.empty() && token.size() <= 10, "Invalid entity count");
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < token.size(); ++i) {
        Check(token[i] >= '0' && token[i] <= '9', "Invalid entity count");
        value = value * 10 + unsigned(token[i] - '0');
    }
    Check(value <= (std::numeric_limits<std::uint32_t>::max)() &&
          value <= (std::numeric_limits<std::size_t>::max)(), "Entity count exceeds host range");
    return std::size_t(value);
}
std::vector<std::size_t> Header(Cursor& cursor, const char* key, std::size_t numbers) {
    const std::vector<std::string> words = cursor.Line();
    Check(words.size() == numbers + 1 && words[0] == key, "Unexpected entity header field");
    std::vector<std::size_t> values;
    for (std::size_t i = 1; i < words.size(); ++i) values.push_back(Count(words[i]));
    return values;
}
std::vector<std::uint32_t> References(Cursor& cursor, std::size_t count, const EntityLimits& limits) {
    Check(count <= cursor.Remaining() / 4, "Truncated entity reference counts");
    std::vector<std::uint32_t> counts; counts.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t value = cursor.U32();
        Check(value <= limits.properties, "Entity reference count exceeds configured limit");
        counts.push_back(value);
    }
    return counts;
}
std::string TextField(const EntityRecord& entity, const char* key) {
    const EntityProperty* property = entity.Find(key);
    if (!property) return std::string();
    Check(property->kind == EntityProperty::Text, "Expected textual entity placement field");
    return property->text;
}
bool NumericField(const EntityRecord& entity, const char* key, float* output, std::size_t count) {
    const EntityProperty* property = entity.Find(key);
    if (!property) return false;
    if (property->kind == EntityProperty::Integer && count == 1) {
        output[0] = static_cast<float>(property->integer); return true;
    }
    Check(property->kind == EntityProperty::Floats && property->numbers.size() == count,
          "Unexpected numeric entity placement field size");
    std::copy(property->numbers.begin(), property->numbers.end(), output);
    return true;
}
} // namespace

const EntityProperty* EntityRecord::Find(const std::string& key) const {
    const std::string folded = Fold(key);
    for (std::size_t i = 0; i < properties.size(); ++i)
        if (Fold(properties[i].key) == folded) return &properties[i];
    return NULL;
}

EntityDocument DecodeEntities4(const std::vector<unsigned char>& data, const EntityLimits& limits) {
    Check(data.size() <= limits.sourceBytes, "Entity source exceeds configured byte limit");
    Cursor cursor(data);
    Check(Header(cursor, "Version", 1)[0] == 4, "Unsupported entity format version");
    Header(cursor, "binary", 0);
    const std::vector<std::size_t> entityHeader = Header(cursor, "entities", 2);
    const std::vector<std::size_t> keyHeader = Header(cursor, "keys", 2);
    const std::vector<std::size_t> valueHeader = Header(cursor, "values", 2);
    const std::size_t integerCount = Header(cursor, "intValues", 1)[0];
    const std::size_t floatCount = Header(cursor, "floatValues", 1)[0];
    Check(entityHeader[0] <= limits.entities && entityHeader[1] <= data.size(), "Entity count/span exceeds configured limit");
    Check(keyHeader[0] <= limits.dictionaryEntries && valueHeader[0] <= limits.dictionaryEntries,
          "Entity dictionary entry count exceeds configured limit");
    Check(keyHeader[1] <= limits.dictionaryBytes && valueHeader[1] <= limits.dictionaryBytes - keyHeader[1],
          "Entity dictionary bytes exceed configured limit");
    Check(integerCount == 0, "Unobserved nonempty entity integer pool is unsupported");
    Check(floatCount <= limits.numericValues && floatCount <= cursor.Remaining() / 4,
          "Entity numeric pool exceeds configured limit or source");
    std::vector<float> floats; floats.reserve(floatCount);
    for (std::size_t i = 0; i < floatCount; ++i) floats.push_back(FloatBits(cursor.U32()));
    const std::vector<std::string> keys = cursor.Strings(keyHeader[0], keyHeader[1], true);
    const std::vector<std::uint32_t> expectedKeyRefs = References(cursor, keys.size(), limits);
    const std::vector<std::string> strings = cursor.Strings(valueHeader[0], valueHeader[1], false);
    const std::vector<std::uint32_t> expectedStringRefs = References(cursor, strings.size(), limits);
    std::vector<std::uint32_t> keyRefs(keys.size(), 0), stringRefs(strings.size(), 0);
    const std::size_t recordsBegin = cursor.Position();
    Check(entityHeader[1] <= cursor.Remaining(), "Entity record span exceeds source");
    Check(entityHeader[0] <= entityHeader[1] / 4, "Entity count cannot fit declared record span");
    const std::size_t recordsEnd = recordsBegin + entityHeader[1];
    std::size_t decodedStrings = 0, decodedNumbers = 0;
    EntityDocument document; document.entities.reserve(entityHeader[0]);
    for (std::size_t e = 0; e < entityHeader[0]; ++e) {
        Check(cursor.Position() <= recordsEnd && recordsEnd - cursor.Position() >= 4,
              "Truncated entity property count within record span");
        const std::size_t count = cursor.U32();
        Check(document.propertyCount <= limits.properties && count <= limits.properties - document.propertyCount &&
              count <= (recordsEnd - cursor.Position()) / 16, "Entity property count exceeds limit or record span");
        EntityRecord entity; entity.properties.reserve(count);
        std::set<std::string> distinctKeys;
        for (std::size_t p = 0; p < count; ++p) {
            const std::uint32_t key = cursor.U32(), type = cursor.U32(), components = cursor.U32(), index = cursor.U32();
            Check(key < keys.size(), "Entity property key index out of range");
            Check(components != 0 && components <= limits.components, "Entity component count exceeds configured limit");
            Check(distinctKeys.insert(Fold(keys[key])).second, "Duplicate entity property key");
            Check(decodedStrings <= limits.decodedStringBytes && keys[key].size() <= limits.decodedStringBytes - decodedStrings,
                  "Decoded entity strings exceed configured limit");
            decodedStrings += keys[key].size();
            EntityProperty property;
            property.key = keys[key]; property.sourceType = type; property.sourceIndex = index;
            ++keyRefs[key];
            if (type == 0) {
                Check(components == 1 && index < strings.size(), "Entity string index/count out of range");
                Check(strings[index].size() <= limits.decodedStringBytes - decodedStrings,
                      "Decoded entity strings exceed configured limit");
                decodedStrings += strings[index].size();
                property.kind = EntityProperty::Text; property.text = strings[index]; ++stringRefs[index];
            } else if (type == 1) {
                Check(index <= floats.size() && components <= floats.size() - index, "Entity float pool reference out of range");
                Check(decodedNumbers <= limits.decodedNumericValues && components <= limits.decodedNumericValues - decodedNumbers,
                      "Decoded entity numeric values exceed configured limit");
                decodedNumbers += components;
                property.kind = EntityProperty::Floats;
                property.numbers.assign(floats.begin() + index, floats.begin() + index + components);
            } else if (type == 3) {
                Check(components == 1, "Entity float literal component count must be one");
                Check(decodedNumbers < limits.decodedNumericValues, "Decoded entity numeric values exceed configured limit");
                ++decodedNumbers;
                property.kind = EntityProperty::Floats; property.numbers.push_back(FloatBits(index));
            } else if (type == 4) {
                Check(components == 1, "Entity integer literal component count must be one");
                property.kind = EntityProperty::Integer; property.integer = SignedBits(index);
            } else Check(false, "Unobserved entity property type is unsupported");
            entity.properties.push_back(std::move(property));
        }
        document.propertyCount += count;
        document.entities.push_back(std::move(entity));
    }
    Check(cursor.Position() - recordsBegin == entityHeader[1], "Entity record byte span mismatch");
    Check(keyRefs == expectedKeyRefs && stringRefs == expectedStringRefs, "Entity dictionary reference counts disagree");
    cursor.End();
    return document;
}

EntityPlacement ReadEntityPlacement(const EntityRecord& entity) {
    EntityPlacement placement;
    placement.name = TextField(entity, "name");
    placement.classname = TextField(entity, "classname");
    placement.model = TextField(entity, "model");
    placement.hasOrigin = NumericField(entity, "origin", placement.origin, 3);
    placement.hasRotation = NumericField(entity, "rotation", placement.rotation, 9);
    placement.hasAngle = NumericField(entity, "angle", &placement.angle, 1);
    return placement;
}
}
