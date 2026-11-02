// SPDX-License-Identifier: GPL-3.0-or-later
#include "Entities.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace wolf2009 { namespace {
typedef std::vector<unsigned char> Bytes;
void Assert(bool value) { if (!value) throw std::runtime_error("Assertion failed"); }
void U32(Bytes& b, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(value >> (i * 8)));
}
void Set(Bytes& b, std::size_t at, std::uint32_t value) {
    Assert(at + 4 <= b.size());
    for (unsigned int i = 0; i < 4; ++i) b[at + i] = static_cast<unsigned char>(value >> (i * 8));
}
void Float(Bytes& b, float value) {
    std::uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); U32(b, bits);
}
void String(Bytes& b, const std::string& value) { b.insert(b.end(), value.begin(), value.end()); b.push_back(0); }
void Align(Bytes& b) { while (b.size() & 3) b.push_back(0); }
void Pair(Bytes& b, std::uint32_t key, std::uint32_t type, std::uint32_t count, std::uint32_t index) {
    U32(b, key); U32(b, type); U32(b, count); U32(b, index);
}
void Fails(const std::function<void()>& fn) {
    bool failed = false; try { fn(); } catch (const std::exception&) { failed = true; }
    Assert(failed);
}
struct Fixture {
    Bytes data;
    std::size_t floats, keys, keyRefs, strings, stringRefs, records, header;
};
Fixture Sample() {
    const std::vector<std::string> keys = {"classname", "name", "origin", "rotation", "model", "angle", "enabled", "opacity"};
    const std::vector<std::string> strings = {"func_static", "synthetic_entity", "models/synthetic/box.md5r"};
    Bytes keyBytes, stringBytes;
    for (std::size_t i = 0; i < keys.size(); ++i) String(keyBytes, keys[i]);
    for (std::size_t i = 0; i < strings.size(); ++i) String(stringBytes, strings[i]);
    const std::string header = "Version 4\nbinary\nentities 1 132\nkeys 8 " + std::to_string(keyBytes.size()) +
        "\nvalues 3 " + std::to_string(stringBytes.size()) + "\nintValues 0\nfloatValues 12\n";
    Fixture f; f.data.assign(header.begin(), header.end()); f.header = header.size(); f.floats = f.data.size();
    const float numbers[12] = {10, 20, 30, 0, -1, 0, 1, 0, 0, 0, 0, 1};
    for (std::size_t i = 0; i < 12; ++i) Float(f.data, numbers[i]);
    f.keys = f.data.size(); f.data.insert(f.data.end(), keyBytes.begin(), keyBytes.end()); Align(f.data);
    f.keyRefs = f.data.size(); for (std::size_t i = 0; i < keys.size(); ++i) U32(f.data, 1);
    f.strings = f.data.size(); f.data.insert(f.data.end(), stringBytes.begin(), stringBytes.end()); Align(f.data);
    f.stringRefs = f.data.size(); for (std::size_t i = 0; i < strings.size(); ++i) U32(f.data, 1);
    f.records = f.data.size(); U32(f.data, 8);
    Pair(f.data, 0, 0, 1, 0); Pair(f.data, 1, 0, 1, 1); Pair(f.data, 2, 1, 3, 0);
    Pair(f.data, 3, 1, 9, 3); Pair(f.data, 4, 0, 1, 2); Pair(f.data, 5, 4, 1, 90);
    Pair(f.data, 6, 4, 1, 0xffffffffU); Pair(f.data, 7, 3, 1, 0x3f400000U);
    f.data.push_back('\n'); return f;
}
} // namespace

int RunEntityTests() {
    unsigned int passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& fn) {
        try { fn(); ++passed; }
        catch (const std::exception& error) { ++failed; std::cerr << "entity test " << name << ": " << error.what() << '\n'; }
    };
    test("typed values and explicit placement", [] {
        const EntityDocument d = DecodeEntities4(Sample().data);
        Assert(d.entities.size() == 1 && d.propertyCount == 8);
        const EntityRecord& e = d.entities[0]; const EntityPlacement p = ReadEntityPlacement(e);
        Assert(p.classname == "func_static" && p.name == "synthetic_entity" && p.model == "models/synthetic/box.md5r");
        Assert(p.hasOrigin && p.origin[0] == 10 && p.origin[1] == 20 && p.origin[2] == 30);
        Assert(p.hasRotation && p.rotation[1] == -1 && p.rotation[3] == 1);
        Assert(p.hasAngle && p.angle == 90 && e.Find("ENABLED")->integer == -1);
        Assert(e.Find("opacity")->numbers[0] == .75f && e.Find("opacity")->sourceType == 3);
        Assert(e.Find("missing") == NULL);
    });
    test("placement defaults preserve missing-field status", [] {
        const EntityPlacement p = ReadEntityPlacement(EntityRecord());
        Assert(!p.hasOrigin && !p.hasRotation && !p.hasAngle && p.rotation[0] == 1 && p.rotation[4] == 1 && p.rotation[8] == 1);
    });
    test("empty document", [] {
        const std::string h = "Version 4\nbinary\nentities 0 0\nkeys 0 0\nvalues 0 0\nintValues 0\nfloatValues 0\n";
        Bytes b(h.begin(), h.end()); Align(b); b.push_back('\n');
        Assert(DecodeEntities4(b).entities.empty());
    });
    test("newline variations", [] {
        Bytes b = Sample().data; b.pop_back(); Assert(DecodeEntities4(b).entities.size() == 1);
        b.push_back('\r'); b.push_back('\n'); Assert(DecodeEntities4(b).entities.size() == 1);
    });
    test("truncated header", [] { Fails([] { DecodeEntities4(Bytes()); }); });
    test("wrong version", [] { Bytes b = Sample().data; b[8] = '5'; Fails([&] { DecodeEntities4(b); }); });
    test("unsupported nonempty int pool", [] {
        Bytes b = Sample().data; const std::string text(b.begin(), b.end()); const std::size_t at = text.find("intValues 0");
        b[at + 10] = '1'; Fails([&] { DecodeEntities4(b); });
    });
    test("truncated numeric pool", [] { Fixture f = Sample(); f.data.resize(f.floats + 4); Fails([&] { DecodeEntities4(f.data); }); });
    test("nonfinite pool float", [] { Fixture f = Sample(); Set(f.data, f.floats, 0x7f800000U); Fails([&] { DecodeEntities4(f.data); }); });
    test("unterminated string table", [] {
        Fixture f = Sample(); for (std::size_t i = f.keys; i < f.keyRefs; ++i) if (!f.data[i]) f.data[i] = 'x';
        Fails([&] { DecodeEntities4(f.data); });
    });
    test("reference counts", [] { Fixture f = Sample(); Set(f.data, f.keyRefs, 2); Fails([&] { DecodeEntities4(f.data); }); });
    test("string reference counts", [] { Fixture f = Sample(); Set(f.data, f.stringRefs, 0); Fails([&] { DecodeEntities4(f.data); }); });
    test("property count", [] { Fixture f = Sample(); Set(f.data, f.records, 0xffffffffU); Fails([&] { DecodeEntities4(f.data); }); });
    test("key index", [] { Fixture f = Sample(); Set(f.data, f.records + 4, 8); Fails([&] { DecodeEntities4(f.data); }); });
    test("unknown value type", [] { Fixture f = Sample(); Set(f.data, f.records + 8, 2); Fails([&] { DecodeEntities4(f.data); }); });
    test("zero components", [] { Fixture f = Sample(); Set(f.data, f.records + 12, 0); Fails([&] { DecodeEntities4(f.data); }); });
    test("string index", [] { Fixture f = Sample(); Set(f.data, f.records + 16, 3); Fails([&] { DecodeEntities4(f.data); }); });
    test("float array index", [] { Fixture f = Sample(); Set(f.data, f.records + 4 + 2 * 16 + 12, 12); Fails([&] { DecodeEntities4(f.data); }); });
    test("duplicate key", [] { Fixture f = Sample(); Set(f.data, f.records + 4 + 16, 0); Fails([&] { DecodeEntities4(f.data); }); });
    test("nonfinite literal", [] { Fixture f = Sample(); Set(f.data, f.records + 4 + 7 * 16 + 12, 0x7fc00000U); Fails([&] { DecodeEntities4(f.data); }); });
    test("unexpected suffix", [] { Bytes b = Sample().data; b.push_back('x'); Fails([&] { DecodeEntities4(b); }); });
    test("source and entity limits", [] {
        const Bytes b = Sample().data; EntityLimits l; l.sourceBytes = 1; Fails([&] { DecodeEntities4(b, l); });
        l = EntityLimits(); l.entities = 0; Fails([&] { DecodeEntities4(b, l); });
    });
    test("dictionary limits", [] {
        const Bytes b = Sample().data; EntityLimits l; l.dictionaryEntries = 1; Fails([&] { DecodeEntities4(b, l); });
        l = EntityLimits(); l.dictionaryBytes = 1; Fails([&] { DecodeEntities4(b, l); });
    });
    test("numeric and property limits", [] {
        const Bytes b = Sample().data; EntityLimits l; l.numericValues = 1; Fails([&] { DecodeEntities4(b, l); });
        l = EntityLimits(); l.properties = 1; Fails([&] { DecodeEntities4(b, l); });
        l = EntityLimits(); l.components = 2; Fails([&] { DecodeEntities4(b, l); });
    });
    test("decoded allocation limits", [] {
        const Bytes b = Sample().data; EntityLimits l; l.decodedStringBytes = 1; Fails([&] { DecodeEntities4(b, l); });
        l = EntityLimits(); l.decodedNumericValues = 1; Fails([&] { DecodeEntities4(b, l); });
    });
    test("wrong placement field type", [] {
        EntityRecord e; EntityProperty p; p.key = "origin"; p.text = "not a vector"; e.properties.push_back(p);
        Fails([&] { ReadEntityPlacement(e); });
    });
    std::cout << "wolf2009 entity self-test: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
}
