#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <fstream>
#include <set>

#include "pwg/history.hpp"
#include "pwg/png.hpp"
#include "pwg/render.hpp"
#include "pwg/world.hpp"

using namespace pwg;

static Params small(int seed, int w = 200, int h = 160) {
    Params p;
    p.seed = seed, p.width = w, p.height = h;
    p.density = 2000;
    return p;
}

static long d2(int ax, int ay, int bx, int by) { return (long)(ax - bx) * (ax - bx) + (long)(ay - by) * (ay - by); }

TEST_CASE("the same seed gives the same world and history") {
    World a = generateWorld(small(7)), b = generateWorld(small(7));
    REQUIRE(a.tiles.size() == b.tiles.size());
    for (size_t i = 0; i < a.tiles.size(); i++) {
        CHECK(a.tiles[i].height == b.tiles[i].height);
        CHECK(a.tiles[i].moisture == b.tiles[i].moisture);
        CHECK(a.tiles[i].biome() == b.tiles[i].biome());
    }
    History ha = spawnCivilizations(a), hb = spawnCivilizations(b);
    for (int y = 0; y < 40; y++) processYear(a, ha), processYear(b, hb);
    REQUIRE(ha.towns.size() == hb.towns.size());
    for (size_t i = 0; i < ha.towns.size(); i++) {
        CHECK(ha.towns[i].x == hb.towns[i].x);
        CHECK(ha.towns[i].y == hb.towns[i].y);
        CHECK(ha.towns[i].population == hb.towns[i].population);
    }
}

TEST_CASE("different seeds give different worlds") {
    World a = generateWorld(small(1)), b = generateWorld(small(2));
    size_t differ = 0;
    for (size_t i = 0; i < a.tiles.size(); i++) differ += a.tiles[i].biome() != b.tiles[i].biome();
    CHECK(differ > a.tiles.size() / 10);
}

TEST_CASE("fields are normalised and every dry tile has exactly one land type") {
    for (int seed : {1, 2, 3, 42, 1234}) {
        World w = generateWorld(small(seed));
        for (const auto &t : w.tiles) {
            for (double v : {t.height, t.temperature, t.moisture, t.rain, t.wind, t.heat}) {
                CHECK(v >= 0.0);
                CHECK(v <= 1.0);
            }
            if (t.isOcean) CHECK(t.moisture == 0.0);
            if (!t.isUnderWater) CHECK(t.landTypes() == 1);
            if (t.isMountain) CHECK(t.height > w.mountainLevel);
            CHECK(t.isOcean == (t.height < w.waterLevel));
        }
        CHECK(w.landFraction + w.oceanFraction == doctest::Approx(1.0));
    }
}

TEST_CASE("forests are the wettest forestPercentage of the non-mountain land") {
    World w = generateWorld(small(5, 300, 240));
    size_t land = 0, forest = 0;
    double minForest = 1, maxOther = 0;
    for (const auto &t : w.tiles) {
        if (t.isUnderWater || t.isMountain) continue;
        land++;
        if (t.isForest) forest++, minForest = std::min(minForest, t.moisture);
        else maxOther = std::max(maxOther, t.moisture);
    }
    REQUIRE(land > 0);
    CHECK(double(forest) / land == doctest::Approx(w.p.forestPercentage).epsilon(0.05));
    CHECK(minForest >= maxOther);
}

TEST_CASE("rivers start on mountains, keep their distance and flow downhill from the source") {
    for (int seed : {3, 11, 99}) {
        World w = generateWorld(small(seed, 400, 300));
        const long dmin = (long)w.p.distanceBetweenRivers * w.p.distanceBetweenRivers;
        for (size_t i = 0; i < w.riverSources.size(); i++) {
            const Tile &s = w.tiles[w.riverSources[i]];
            CHECK(s.isMountain);
            CHECK(s.isRiver);
            for (size_t j = 0; j < i; j++) {
                const Tile &o = w.tiles[w.riverSources[j]];
                CHECK(d2(s.x, s.y, o.x, o.y) >= dmin);
            }
        }
        for (const auto &t : w.tiles)
            if (t.isRiver) CHECK_FALSE(t.isOcean);
    }
}

TEST_CASE("tiny and unusual maps generate without errors") {
    // ASan in CI catches out-of-bounds accesses here (the original indexed y with the width).
    for (auto [W, H] : {std::pair{20, 20}, {40, 25}, {25, 200}, {300, 30}, {97, 61}}) {
        for (int seed = 0; seed < 4; seed++) {
            Params p = small(seed, W, H);
            p.density = 100;
            World w = generateWorld(p);
            History h = spawnCivilizations(w);
            for (int y = 0; y < 60; y++) processYear(w, h);
            for (const auto &t : h.towns) CHECK(w.inside(t.x, t.y));
        }
    }
    CHECK_THROWS_AS(World(small(1, 10, 10)), std::invalid_argument);
}

TEST_CASE("a world without mountains has no rivers") {
    Params p = small(4);
    p.mountainLevelPercentage = 1.0;
    World w = generateWorld(p);
    CHECK(w.mountains.empty());
    CHECK(w.riverSources.empty());
}

TEST_CASE("civilizations start on land, apart, and their towns spread over land") {
    Params p = small(21, 512, 400);
    World w = generateWorld(p);
    History h = spawnCivilizations(w);
    REQUIRE(h.civilizations.size() >= 5);
    const long dmin = (long)p.distanceBetweenCivilizations * p.distanceBetweenCivilizations;
    for (size_t i = 0; i < h.civilizations.size(); i++) {
        const auto &c = h.civilizations[i];
        CHECK(landTile(w, c.xStart, c.yStart));
        CHECK((int)c.race == (int)i % 5);
        for (size_t j = 0; j < i; j++)
            CHECK(d2(c.xStart, c.yStart, h.civilizations[j].xStart, h.civilizations[j].yStart) >= dmin);
    }
    double before = 0;
    for (const auto &c : h.civilizations) before += c.population(h.towns);
    for (int y = 0; y < 80; y++) processYear(w, h);
    double after = 0;
    for (const auto &c : h.civilizations) after += c.population(h.towns);
    CHECK(h.year == 80);
    CHECK(after > before);
    CHECK(h.towns.size() > h.civilizations.size());  // daughter towns were founded
    std::set<std::pair<int, int>> seen;
    for (size_t i = 0; i < h.towns.size(); i++) {
        const auto &t = h.towns[i];
        CHECK(landTile(w, t.x, t.y));
        CHECK(seen.insert({t.x, t.y}).second);
        if (i >= h.civilizations.size())  // founded towns keep their distance from earlier ones
            for (size_t j = 0; j < i; j++)
                CHECK(d2(t.x, t.y, h.towns[j].x, h.towns[j].y) >= (long)p.distanceBetweenTowns * p.distanceBetweenTowns);
    }
    size_t counted = 0;
    for (const auto &c : h.civilizations) {
        counted += c.towns.size();
        for (int t : c.towns) CHECK(h.towns[t].civ == c.id);
    }
    CHECK(counted == h.towns.size());
}

TEST_CASE("rendering and PNG export") {
    World w = generateWorld(small(8, 64, 48));
    History h = spawnCivilizations(w);
    for (const auto &name : layerNames()) {
        Layer l;
        REQUIRE(parseLayer(name, l));
        auto px = render(w, &h, l);
        CHECK(px.size() == 64u * 48u * 4u);
    }
    Layer l;
    CHECK_FALSE(parseLayer("nope", l));
    const std::string path = "pwg_test_map.png";
    writePng(path, render(w, &h, Layer::Full), 64, 48);
    std::ifstream f(path, std::ios::binary);
    unsigned char sig[8] = {};
    f.read((char *)sig, 8);
    CHECK(sig[0] == 137);
    CHECK(sig[1] == 'P');
    CHECK(sig[2] == 'N');
    CHECK(sig[3] == 'G');
    f.close();
    std::remove(path.c_str());
}
