// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinCatalog.h"
#include <functional>
#include <iostream>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Refuses(const std::function<void()> &action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Unsupported/malformed skin accepted");
}
GeometryScene Mesh() {
    GeometryScene geometry;
    geometry.vertices = {{0, 0, 0, 0, 0}};
    GeometrySurface surface;
    surface.material = "models/base";
    surface.indices = {0, 0, 0};
    geometry.surfaces.push_back(surface);
    return geometry;
}
} // namespace
int RunSkinCatalogTests() {
    int passed = 0;
    try {
        SkinCatalog catalog;
        catalog.AddSource("skins/test.skin",
                          "// source\nskin skins\\inside { \"models\\base\" \"models/inside\" }\nskin "
                          "noop { models/other models/ignored }\n");
        auto geometry = Mesh();
        const auto *vertices = geometry.vertices.data();
        const auto *indices = geometry.surfaces[0].indices.data();
        auto result = catalog.Apply("SKINS/INSIDE", geometry);
        Need(result.matchedSurfaces == 1 && result.changedSurfaces == 1 &&
                 geometry.surfaces[0].material == "models/inside" &&
                 vertices == geometry.vertices.data() && indices == geometry.surfaces[0].indices.data(),
             "Explicit skin path/remap or stable geometry failed");
        ++passed;
        geometry = Mesh();
        result = catalog.Apply("noop", geometry);
        Need(result.matchedSurfaces == 0 && result.changedSurfaces == 0 &&
                 geometry.surfaces[0].material == "models/base",
             "Unmatched material was fabricated");
        ++passed;
        catalog.AddSource("skins/same.skin", "skin same { models/base models/base }");
        result = catalog.Apply("same", geometry);
        Need(result.matchedSurfaces == 1 && result.changedSurfaces == 0,
             "Self-remap not reported as no-op");
        ++passed;
        Refuses([&] { catalog.Apply("missing", geometry); });
        Need(geometry.surfaces[0].material == "models/base", "Missing skin partially changed materials");
        ++passed;
        catalog.AddSource("skins/wild.skin", "skin wild { * models/default }");
        Refuses([&] { catalog.Apply("wild", geometry); });
        ++passed;
        catalog.AddSource("skins/model.skin",
                          "skin association { model models/a.md5r models/base models/inside }");
        Refuses([&] { catalog.Apply("association", geometry); });
        ++passed;
        catalog.AddSource("skins/dupe.skin",
                          "skin dupe { models/base models/one models/base models/two }");
        Refuses([&] { catalog.Apply("dupe", geometry); });
        ++passed;
        catalog.AddSource("skins/another.skin", "skin same { models/base models/another }");
        Refuses([&] { catalog.Apply("same", geometry); });
        ++passed;
        const auto count = catalog.SkinCount();
        Refuses([&] { catalog.AddSource("skins/bad.skin", "skin valid { a b } skin broken { a }"); });
        Need(catalog.SkinCount() == count && !catalog.Contains("valid"),
             "Partial skin source escaped transactional parse");
        ++passed;
        Refuses([&] { catalog.AddSource("skins/bad.skin", "skin broken { \"unterminated }"); });
        Refuses([&] { catalog.AddSource("skins/bad.skin", "/* unterminated"); });
        ++passed;
        Refuses([&] { catalog.AddSource("skins/bad.skin", "skin ../escape { a b }"); });
        ++passed;
        SkinLimits limits;
        limits.sourceBytes = 2;
        SkinCatalog limited(limits);
        Refuses([&] { limited.AddSource("skins/a.skin", "skin a { x y }"); });
        limits = SkinLimits();
        limits.skins = 0;
        SkinCatalog none(limits);
        Refuses([&] { none.AddSource("skins/a.skin", "skin a { x y }"); });
        limits = SkinLimits();
        limits.rules = 0;
        SkinCatalog noRules(limits);
        Refuses([&] { noRules.AddSource("skins/a.skin", "skin a { x y }"); });
        limits = SkinLimits();
        limits.tokens = 2;
        SkinCatalog noTokens(limits);
        Refuses([&] { noTokens.AddSource("skins/a.skin", "skin a { x y }"); });
        ++passed;
        std::cout << "Skin catalog self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Skin catalog test failed after " << passed << ": " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009
#ifdef WOLF2009_SKIN_CATALOG_TEST_MAIN
#include "ZipArchive.h"
#include <fstream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            return wolf2009::RunSkinCatalogTests();
        std::ifstream in(argv[1], std::ios::binary);
        if (!in)
            throw std::runtime_error("Cannot open private PK4");
        const auto zip = wolf2009::ZipArchive::Scan(in);
        wolf2009::SkinCatalog catalog;
        unsigned files = 0;
        for (std::size_t i = 0; i < zip.Entries().size(); i++) {
            const auto &e = zip.Entries()[i];
            if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".skin") != 0)
                continue;
            const auto data = zip.ReadEntry(in, i);
            catalog.AddSource(e.name, std::string(data.begin(), data.end()));
            ++files;
        }
        wolf2009::GeometryScene geometry;
        wolf2009::GeometrySurface surface;
        surface.material = "models/vehicles/box_car";
        geometry.surfaces.push_back(surface);
        const auto applied = catalog.Apply("models/vehicles/box_car_in", geometry);
        if (applied.changedSurfaces != 1 ||
            geometry.surfaces[0].material != "models/vehicles/box_car_in")
            throw std::runtime_error("Real explicit box-car skin failed");
        std::cout << "Retail skin PASS sources=" << files << " skins=" << catalog.SkinCount()
                  << " matched=" << applied.matchedSurfaces
                  << " target=" << geometry.surfaces[0].material << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
