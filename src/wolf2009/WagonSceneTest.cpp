// SPDX-License-Identifier: GPL-3.0-or-later
#include "WagonScene.h"
#include "Entities.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void Reject(F op) {
    bool failed = false;
    try {
        op();
    } catch (const std::exception &) {
        failed = true;
    }
    Need(failed, "Unsupported wagon scene accepted");
}
bool Near(double a, double b) {
    return std::abs(a - b) < 1e-5;
}
void U(Bytes &b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(v >> (i * 8)));
}
void Set(Bytes &b, std::size_t o, std::uint32_t v) {
    Need(o + 4 <= b.size(), "Synthetic writer range");
    for (int i = 0; i < 4; ++i)
        b[o + i] = static_cast<unsigned char>(v >> (i * 8));
}
void F(Bytes &b, std::size_t o, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    Set(b, o, bits);
}
void String(Bytes &b, const std::string &s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}
void At(Bytes &b, std::size_t o, const std::string &s) {
    Need(o + s.size() < b.size(), "Synthetic text range");
    std::copy(s.begin(), s.end(), b.begin() + o);
    b[o + s.size()] = 0;
}
void Pad(Bytes &b) {
    while (b.size() % 4)
        b.push_back(0);
}
std::size_t Align(std::size_t n) {
    return (n + 15) / 16 * 16;
}
void Text(EntityRecord &e, const char *key, const std::string &value) {
    EntityProperty p;
    p.key = key;
    p.text = value;
    e.properties.push_back(p);
}
void Numbers(EntityRecord &e, const char *key, std::initializer_list<float> values) {
    EntityProperty p;
    p.key = key;
    p.kind = EntityProperty::Floats;
    p.numbers = values;
    e.properties.push_back(p);
}
EntityProperty &Property(EntityRecord &e, const char *key) {
    for (auto &p : e.properties)
        if (p.key == key)
            return p;
    throw std::runtime_error("Synthetic property missing");
}
Bytes Entities(const EntityDocument &doc) {
    std::vector<std::string> keys, values;
    std::map<std::string, std::uint32_t> ki, vi;
    std::vector<std::uint32_t> kr, vr;
    std::vector<float> numbers;
    Bytes records;
    for (const auto &e : doc.entities) {
        U(records, static_cast<std::uint32_t>(e.properties.size()));
        for (const auto &p : e.properties) {
            if (!ki.count(p.key)) {
                ki[p.key] = static_cast<std::uint32_t>(keys.size());
                keys.push_back(p.key);
                kr.push_back(0);
            }
            const auto key = ki[p.key];
            ++kr[key];
            U(records, key);
            if (p.kind == EntityProperty::Text) {
                if (!vi.count(p.text)) {
                    vi[p.text] = static_cast<std::uint32_t>(values.size());
                    values.push_back(p.text);
                    vr.push_back(0);
                }
                const auto value = vi[p.text];
                ++vr[value];
                U(records, 0);
                U(records, 1);
                U(records, value);
            } else {
                Need(p.kind == EntityProperty::Floats, "Synthetic ENTS kind");
                U(records, 1);
                U(records, static_cast<std::uint32_t>(p.numbers.size()));
                U(records, static_cast<std::uint32_t>(numbers.size()));
                numbers.insert(numbers.end(), p.numbers.begin(), p.numbers.end());
            }
        }
    }
    Bytes kb, vb;
    for (const auto &s : keys)
        String(kb, s);
    for (const auto &s : values)
        String(vb, s);
    const std::string h = "Version 4\nbinary\nentities " + std::to_string(doc.entities.size()) + " " +
                          std::to_string(records.size()) + "\nkeys " + std::to_string(keys.size()) + " " +
                          std::to_string(kb.size()) + "\nvalues " + std::to_string(values.size()) + " " +
                          std::to_string(vb.size()) + "\nintValues 0\nfloatValues " +
                          std::to_string(numbers.size()) + "\n";
    Bytes out(h.begin(), h.end());
    for (float f : numbers) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        U(out, bits);
    }
    out.insert(out.end(), kb.begin(), kb.end());
    Pad(out);
    for (auto n : kr)
        U(out, n);
    out.insert(out.end(), vb.begin(), vb.end());
    Pad(out);
    for (auto n : vr)
        U(out, n);
    out.insert(out.end(), records.begin(), records.end());
    out.push_back('\n');
    return out;
}
Bytes Model() {
    Bytes vertices(192, 0);
    for (int i = 0; i < 3; ++i)
        F(vertices, i * 64 + 12, 1);
    F(vertices, 64, 1);
    F(vertices, 128 + 4, 1);
    F(vertices, 64 + 56, .5f);
    F(vertices, 128 + 60, .5f);
    const Bytes indices = {0, 0, 1, 0, 2, 0};
    std::string s = "MemSize 1\nMD5RVersion 1\nVertexBuffer[1]{VertexBuffer{VertexFormat{Position 4 Normal "
                    "Tangent Binormal DiffuseColor TexCoord 2 0} Vertex[3]{bin 192\n";
    s.append(reinterpret_cast<const char *>(vertices.data()), vertices.size());
    s += "}}}IndexBuffer[1]{IndexBuffer{BitDepth 16 Index[3]{bin 6\n";
    s.append(reinterpret_cast<const char *>(indices.data()), indices.size());
    s += "}}}Mesh[1]{Mesh{Material \"synthetic/wagon\" DrawBuffers 0 0 "
         "PrimBatch[1]{PrimBatch{DrawIndexedTriList 0 3 0 1}} Bounds 0 0 0 1 1 0}}Bounds 0 0 0 1 1 0\n";
    return {s.begin(), s.end()};
}
Bytes Collision(bool duplicateFloor, float floorShift) {
    Bytes data(1024, 0);
    std::map<std::uint32_t, std::string> types = {{0, "hkRootLevelContainer"},      {64, "hkpRigidBody"},
                                                  {576, "hkpMoppBvTreeShape"},      {640, "hkpListShape"},
                                                  {752, "hkpConvexTranslateShape"}, {800, "hkpBoxShape"},
                                                  {848, "hkpCylinderShape"}};
    std::map<std::uint32_t, std::uint32_t> links = {
        {176, 896}, {80, 576}, {628, 640}, {664, 704}, {704, 752}, {720, duplicateFloor ? 752u : 848u},
        {776, 800}};
    At(data, 896, "models/vehicles/box_car_a");
    Set(data, 668, 2);
    F(data, 792, (-1 + floorShift) * .0254f);
    F(data, 832, 32 * .0254f);
    F(data, 836, 64 * .0254f);
    F(data, 840, .0254f);
    for (int i = 0; i < 3; ++i)
        F(data, 64 + 224 + i * 20, 1);
    F(data, 348, 1);
    F(data, 396, 1);
    F(data, 412, 1);
    Bytes names(16, 0);
    std::map<std::string, std::uint32_t> no;
    for (const auto &t : types)
        if (!no.count(t.second)) {
            const auto o = names.size();
            no[t.second] = static_cast<std::uint32_t>(o);
            names.resize(o + t.second.size() + 1);
            At(names, o, t.second);
        }
    names.resize(Align(names.size()), 255);
    data.resize(Align(data.size()), 0);
    const std::size_t absolute = 208 + names.size(), local = data.size(),
                      global = Align(local + links.size() * 8), end = Align(global + types.size() * 12);
    Bytes b(absolute + end, 255);
    std::fill(b.begin(), b.begin() + 208, 0);
    Set(b, 0, 0x57e0e057);
    Set(b, 4, 0x10c0c010);
    Set(b, 12, 5);
    b[16] = 4;
    b[17] = 1;
    b[18] = 0;
    b[19] = 1;
    Set(b, 20, 3);
    Set(b, 24, 2);
    Set(b, 36, no["hkRootLevelContainer"]);
    At(b, 40, "Havok-5.6.0-r1");
    const char *tags[] = {"__classnames__", "__types__", "__data__"};
    for (int i = 0; i < 3; ++i) {
        const std::size_t h = 64 + i * 48;
        At(b, h, tags[i]);
        Set(b, h + 20, static_cast<std::uint32_t>(i == 0 ? 208 : absolute));
        for (int k = 0; k < 6; ++k)
            Set(b, h + 24 + k * 4,
                static_cast<std::uint32_t>(i == 0   ? names.size()
                                           : i == 1 ? 0
                                           : k == 0 ? local
                                           : k <= 2 ? global
                                                    : end));
    }
    std::copy(names.begin(), names.end(), b.begin() + 208);
    std::copy(data.begin(), data.end(), b.begin() + absolute);
    std::size_t p = absolute + local;
    for (const auto &x : links) {
        Set(b, p, x.first);
        Set(b, p + 4, x.second);
        p += 8;
    }
    p = absolute + global;
    for (const auto &t : types) {
        Set(b, p, t.first);
        Set(b, p + 4, 0);
        Set(b, p + 8, no[t.second]);
        p += 12;
    }
    return b;
}
std::string Line(std::string s) {
    for (char &c : s)
        if (c == '|')
            c = 0;
    return s;
}
std::string ArrivalEdge(const std::string &target, const std::string &mode = "1",
                        const std::string &selector = "0") {
    std::string edge = "\t";
    for (int i = 0; i < 99; i++)
        edge += Line((i == 0    ? "16"
                      : i == 1  ? mode
                      : i == 2  ? target
                      : i == 4  ? selector
                      : i == 98 ? "1.000000"
                                : "0") +
                     std::string("|"));
    return edge + "\n";
}
Bytes Brain(double speed, const std::string &destination, bool redirect, bool arrival = false,
            bool bypass = false) {
    std::string s = Line("h1023|32|32|32|0|\n1|20|0|0|0|^1|1|^74|1024|\n\t");
    for (int i = 0; i < 99; ++i)
        s += Line((i == 0    ? "16"
                   : i == 1  ? "1"
                   : i == 2  ? (redirect ? "8" : "9")
                   : i == 98 ? "1.000000"
                             : "0") +
                  std::string("|"));
    s += Line("\n\t\t-1|\n132|9|0|0|0|^132|16|^131|1024|mMover|Me|mDestination|" + destination +
              "|mSpeed|" + std::to_string(speed) +
              "|mTime|0|mAccelTime|0|mDecelTime|0|mMaxSpeed|0|mMaxTime|0|mUseCustomSounds|false|\n");
    if (arrival)
        s += ArrivalEdge(bypass ? "40" : "31", "3", "2") +
             Line("\t\t-1|\n70|31|0|0|0|^70|0|mRemoveTarget|synthetic_shake|\n") +
             ArrivalEdge("40", "1", "3") + Line("68|40|0|0|0|^68|0|mUseTarget|synthetic_front|\n") +
             ArrivalEdge("41") + Line("68|41|0|0|0|^68|0|mUseTarget|synthetic_middle|\n") +
             ArrivalEdge("42") + Line("68|42|0|0|0|^68|0|mUseTarget|synthetic_rear|\n") +
             ArrivalEdge("8");
    s += Line("30|8|0|0|0|^30|0|\n");
    return {s.begin(), s.end()};
}
typedef std::pair<std::string, Bytes> Resource;
void Chunk(Bytes &out, const char *type, const std::vector<Resource> &files) {
    Bytes plain;
    U(plain, static_cast<std::uint32_t>(files.size()));
    for (const auto &f : files) {
        String(plain, f.first);
        U(plain, static_cast<std::uint32_t>(f.second.size()));
        U(plain, static_cast<std::uint32_t>(f.second.size()));
    }
    Pad(plain);
    for (const auto &f : files) {
        plain.insert(plain.end(), f.second.begin(), f.second.end());
        Pad(plain);
    }
    uLongf size = compressBound(static_cast<uLong>(plain.size()));
    Bytes packed(size);
    Need(compress2(packed.data(), &size, plain.data(), static_cast<uLong>(plain.size()), Z_BEST_SPEED) ==
             Z_OK,
         "Synthetic MPK compression");
    packed.resize(size);
    for (int i = 3; i >= 0; --i)
        out.push_back(type[i]);
    U(out, static_cast<std::uint32_t>(plain.size()));
    U(out, static_cast<std::uint32_t>(packed.size()));
    out.insert(out.end(), packed.begin(), packed.end());
}
struct Fixture {
    EntityDocument doc;
    double speed = 3;
    std::string destination = "synthetic_finish";
    bool redirect = false, duplicateFloor = false, duplicateHkxs = false, missingBrain = false;
    bool arrival = false, bypassArrival = false;
    float floorShift = 0;
    Fixture() {
        EntityRecord actor;
        Text(actor, "classname", "func_mover");
        Text(actor, "name", "starting_boxcar_model");
        Text(actor, "model", "models/vehicles/box_car_a.md5r");
        Numbers(actor, "origin", {10, 20, 30});
        Numbers(actor, "rotation", {1, 0, 0, 0, 1, 0, 0, 0, 1});
        Text(actor, "bind", "starting_boxcar");
        Text(actor, "brain", "trainyard/movetrain.brain");
        Text(actor, "skin", "synthetic/skin");
        doc.entities.push_back(actor);
        EntityRecord spawn;
        Text(spawn, "classname", "info_player_start");
        Text(spawn, "name", "info_player_start");
        Text(spawn, "mission", "mission_trainyard");
        Numbers(spawn, "origin", {11, 21, 31.5f});
        Numbers(spawn, "angle", {90});
        doc.entities.push_back(spawn);
        EntityRecord target;
        Text(target, "classname", "target_null");
        Text(target, "name", destination);
        Numbers(target, "origin", {10, 80, 30});
        doc.entities.push_back(target);
    }
    Bytes Package() const {
        Bytes b;
        U(b, 300);
        Chunk(b, "ENTS", {{"maps/game/trainyard/trainyard", Entities(doc)}});
        Chunk(b, "MODL", {{"models/vehicles/box_car_a", Model()}});
        Chunk(b, "HKXS", {{"maps/game/trainyard/trainyard", Collision(duplicateFloor, floorShift)}});
        if (duplicateHkxs)
            Chunk(b, "HKXS", {{"maps/game/trainyard/trainyard", Collision(false, 0)}});
        if (!missingBrain)
            Chunk(b, "BRAI",
                  {{"brains/trainyard/movetrain",
                    Brain(speed, destination, redirect, arrival, bypassArrival)}});
        return b;
    }
    std::unique_ptr<WagonScene> Load(const WagonSceneOptions &options = WagonSceneOptions(),
                                     const std::string &map = "maps/game/trainyard/trainyard") const {
        const auto b = Package();
        std::istringstream input(std::string(b.begin(), b.end()), std::ios::in | std::ios::binary);
        const auto archive = Archive::Scan(input);
        return WagonScene::Load(input, archive, map, options);
    }
};
} // namespace
int RunWagonSceneTests() {
    int passed = 0;
    try {
        Fixture fixture;
        auto scene = fixture.Load();
        const auto &description = scene->Description();
        Need(description.speed == 3 && description.destinationName == "synthetic_finish" &&
                 description.cartStart.y == 20 && description.cartDestination.y == 80 &&
                 description.playerStart.z == 31.5 && description.playerYawDegrees == 90,
             "Wagon loader hardcoded retail coordinates/speed/target/spawn");
        ++passed;
        Need(scene->PlacedGeometry().vertices.size() == 3 && scene->PlacedGeometry().vertices[1].x == 11 &&
                 scene->PlacedGeometry().vertices[0].y == 20 &&
                 scene->PlacedGeometry().surfaces[0].material == "synthetic/wagon" &&
                 scene->PlacedGeometry().vertices[1].u == .5f,
             "Actual MD5R1 placement/UV/material pipeline failed");
        ++passed;
        Need(scene->State().spawnDelivered && scene->State().moving && scene->State().supportContact &&
                 scene->State().cameraOrigin.z == 95.5 && scene->State().playerFeet.z == 31.5 &&
                 !scene->State().worldWalkable && !description.completeModelCollision &&
                 !description.rawModelContentsComplete &&
                 description.omittedCollision.at("hkpCylinderShape") == 1,
             "Startup/contact/partial-scene scope wrong");
        ++passed;
        const auto *v = scene->PlacedGeometry().vertices.data();
        const auto *idx = scene->PlacedGeometry().surfaces[0].indices.data();
        const auto *state = &scene->State();
        scene->Advance(1);
        Need(scene->State().cartOffset.y == 3 && scene->State().playerFeet.y == 24 &&
                 scene->State().playerFeet.z == 31.5 && scene->State().cameraOrigin.y == 24 &&
                 !scene->State().worldClearanceVerified,
             "Rider/camera did not follow brain-derived delta");
        ++passed;
        scene->Reset();
        Need(scene->State().cartOffset.y == 0 && scene->State().playerFeet.y == 21 &&
                 scene->State().elapsedSeconds == 0 && scene->State().spawnDelivered &&
                 scene->PlacedGeometry().vertices.data() == v &&
                 scene->PlacedGeometry().surfaces[0].indices.data() == idx && &scene->State() == state,
             "Reset copied mesh, lost stable state or failed to restart");
        scene->Advance(1);
        Need(scene->State().playerFeet.y == 24, "Reset failed to deliver reproducible new startup");
        ++passed;
        for (int i = 0; i < 30; ++i)
            scene->Advance(1);
        Need(scene->State().arrived && !scene->State().moving && scene->State().cartOrigin.y == 80 &&
                 scene->State().playerFeet.y == 81 && scene->PlacedGeometry().vertices.data() == v,
             "Arrival overshot, executed unknown continuation or replaced mesh");
        ++passed;
        auto changed = fixture;
        changed.speed = 12;
        Property(changed.doc.entities[2], "origin").numbers = {40, 20, 30};
        auto x = changed.Load();
        x->Advance(1);
        Need(Near(x->State().cartOffset.x, 12) && Near(x->State().cartOffset.y, 0) &&
                 Near(x->State().playerFeet.x, 23),
             "Mover path/speed was hardcoded to trainyard Y");
        ++passed;
        const auto before = scene->State();
        Reject([&] { scene->Advance(-1); });
        Reject([&] { scene->Advance(1.1); });
        Reject([&] { scene->Advance((std::numeric_limits<double>::quiet_NaN)()); });
        scene->Advance(0);
        Need(scene->State().elapsedSeconds == before.elapsedSeconds &&
                 scene->State().sample == before.sample && scene->State().cartOrigin.y == before.cartOrigin.y,
             "Failed/zero Advance changed visible state");
        ++passed;
        WagonSceneOptions tinyTime;
        tinyTime.support.maximumElapsedSeconds = .001;
        auto limited = fixture.Load(tinyTime);
        Reject([&] { limited->Advance(.1); });
        Need(limited->State().cartOffset.y == 0 && limited->State().elapsedSeconds == 0,
             "Failed carry committed visible runtime");
        limited->Advance(.0005);
        Need(Near(limited->State().cartOffset.y, .0015), "Failed carry advanced hidden mover state");
        ++passed;
        changed = fixture;
        changed.redirect = true;
        Reject([&] { changed.Load(); });
        changed = fixture;
        changed.missingBrain = true;
        Reject([&] { changed.Load(); });
        Reject([&] { fixture.Load(WagonSceneOptions(), "maps/game/other/other"); });
        ++passed;
        changed = fixture;
        Property(changed.doc.entities[0], "bind").text = "other_parent";
        Reject([&] { changed.Load(); });
        changed = fixture;
        EntityRecord parent;
        Text(parent, "name", "starting_boxcar");
        changed.doc.entities.push_back(parent);
        Reject([&] { changed.Load(); });
        ++passed;
        changed = fixture;
        Property(changed.doc.entities[0], "classname").text = "func_static";
        Reject([&] { changed.Load(); });
        changed = fixture;
        Property(changed.doc.entities[0], "rotation").numbers = {0, 1, 0, -1, 0, 0, 0, 0, 1};
        Reject([&] { changed.Load(); });
        ++passed;
        changed = fixture;
        changed.doc.entities.push_back(changed.doc.entities[1]);
        Reject([&] { changed.Load(); });
        changed = fixture;
        Property(changed.doc.entities[1], "mission").text = "mission_other";
        Reject([&] { changed.Load(); });
        ++passed;
        changed = fixture;
        Property(changed.doc.entities[1], "origin").numbers = {100, 21, 31.5f};
        Reject([&] { changed.Load(); });
        changed = fixture;
        changed.floorShift = -10;
        Reject([&] { changed.Load(); });
        changed = fixture;
        changed.duplicateFloor = true;
        Reject([&] { changed.Load(); });
        ++passed;
        changed = fixture;
        changed.duplicateHkxs = true;
        Reject([&] { changed.Load(); });
        changed = fixture;
        changed.destination = "missing_target";
        Reject([&] { changed.Load(); });
        changed = fixture;
        Text(changed.doc.entities[2], "bind", "unknown_parent");
        Reject([&] { changed.Load(); });
        ++passed;
        WagonSceneOptions options;
        options.brain.sourceBytes = 1;
        Reject([&] { fixture.Load(options); });
        options = WagonSceneOptions();
        options.collision.boxes = 0;
        Reject([&] { fixture.Load(options); });
        options = WagonSceneOptions();
        options.geometry.vertices = 2;
        Reject([&] { fixture.Load(options); });
        options = WagonSceneOptions();
        options.eyeHeight = 1000;
        Reject([&] { fixture.Load(options); });
        ++passed;
        auto arrivalFixture = fixture;
        arrivalFixture.arrival = true;
        EntityRecord quake;
        Text(quake, "name", "synthetic_shake");
        Text(quake, "classname", "func_earthquake");
        arrivalFixture.doc.entities.push_back(quake);
        unsigned number = 0;
        for (const char *name : {"synthetic_front", "synthetic_middle", "synthetic_rear"}) {
            EntityRecord prop;
            Text(prop, "name", name);
            Text(prop, "classname", "func_static");
            Text(prop, "model", "models/vehicles/box_car_a.md5r");
            Numbers(prop, "origin", {float(100 + number * 20), 50, 30});
            Numbers(prop, "Hide", {1});
            if (number == 2)
                Text(prop, "skin", "synthetic/arrival_skin");
            arrivalFixture.doc.entities.push_back(prop);
            ++number;
        }
        SkinCatalog skins;
        skins.AddSource("skins/test.skin",
                        "skin synthetic/skin { other/material other/replacement } skin "
                        "synthetic/arrival_skin { synthetic/wagon synthetic/inside }");
        WagonSceneOptions arrivalOptions;
        arrivalOptions.enableArrival = true;
        arrivalOptions.skins = &skins;
        auto arrivalScene = arrivalFixture.Load(arrivalOptions);
        Need(arrivalScene->Description().arrivalEnabled && arrivalScene->Description().skinResolved &&
                 arrivalScene->ArrivalInstances().size() == 3 &&
                 arrivalScene->State().levelGeneration == 1 && !arrivalScene->State().arrivalApplied &&
                 arrivalScene->State().visibleArrivalInstances == 0 &&
                 !arrivalScene->IsArrivalInstanceVisible(0),
             "Arrival assembly lost initial hide/skin/generation");
        ++passed;
        const auto *propVertices = arrivalScene->ArrivalInstances()[2].geometry.vertices.data();
        const auto *propIndices =
            arrivalScene->ArrivalInstances()[2].geometry.surfaces[0].indices.data();
        Need(arrivalScene->ArrivalInstances()[2].geometry.surfaces[0].material == "synthetic/inside" &&
                 arrivalScene->ArrivalInstances()[2].skinChangedSurfaces == 1 &&
                 arrivalScene->ArrivalInstances()[0].geometry.vertices[0].x == 100,
             "Real model placement/skin was substituted or ignored");
        ++passed;
        for (int i = 0; i < 20; i++)
            arrivalScene->Advance(1);
        Need(arrivalScene->State().arrived && arrivalScene->State().arrivalApplied &&
                 arrivalScene->State().visibleArrivalInstances == 3 &&
                 arrivalScene->ArrivalChanges().size() == 4 &&
                 !arrivalScene->EntityRegistry()->Find("synthetic_shake")->alive &&
                 arrivalScene->IsArrivalInstanceVisible(2) &&
                 arrivalScene->ArrivalInstances()[2].geometry.vertices.data() == propVertices &&
                 arrivalScene->ArrivalInstances()[2].geometry.surfaces[0].indices.data() == propIndices,
             "Committed arrival did not update registry/visibility once or copied mesh");
        ++passed;
        arrivalScene->Advance(1);
        Need(arrivalScene->ArrivalChanges().size() == 4 && arrivalScene->State().arrivalChangeCount == 4,
             "Post-arrival frame replayed or cleared event batch");
        arrivalScene->Reset();
        Need(arrivalScene->State().levelGeneration == 2 &&
                 arrivalScene->State().visibleArrivalInstances == 0 &&
                 !arrivalScene->State().arrivalApplied && arrivalScene->ArrivalChanges().empty() &&
                 arrivalScene->EntityRegistry()->Find("synthetic_shake")->alive &&
                 !arrivalScene->IsArrivalInstanceVisible(2) &&
                 arrivalScene->ArrivalInstances()[2].geometry.vertices.data() == propVertices,
             "Reset failed to rebuild initial hide/registry/generation without geometry copy");
        ++passed;
        auto bypassFixture = arrivalFixture;
        bypassFixture.bypassArrival = true;
        auto bypassScene = bypassFixture.Load(arrivalOptions);
        for (int i = 0; i < 20; i++)
            bypassScene->Advance(1);
        Need(bypassScene->State().arrivalApplied && bypassScene->State().visibleArrivalInstances == 3 &&
                 bypassScene->ArrivalChanges().size() == 3 &&
                 bypassScene->EntityRegistry()->Find("synthetic_shake")->alive,
             "Causal bypass removed entity or failed to show real instances");
        ++passed;
        auto missingModel = arrivalFixture;
        Property(missingModel.doc.entities.back(), "model").text = "models/missing.md5r";
        Reject([&] { arrivalScene = missingModel.Load(arrivalOptions); });
        Need(arrivalScene->State().levelGeneration == 2 &&
                 arrivalScene->ArrivalInstances()[2].geometry.vertices.data() == propVertices,
             "Failed replacement destroyed previous working scene");
        SkinCatalog missingSkin;
        missingSkin.AddSource("skins/only.skin",
                              "skin synthetic/skin { other/material other/replacement }");
        auto noSkinOptions = arrivalOptions;
        noSkinOptions.skins = &missingSkin;
        Reject([&] { arrivalFixture.Load(noSkinOptions); });
        noSkinOptions.skins = nullptr;
        Reject([&] { arrivalFixture.Load(noSkinOptions); });
        ++passed;
        std::cout << "Wagon scene self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Wagon scene self-test failed after " << passed << ": " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009
