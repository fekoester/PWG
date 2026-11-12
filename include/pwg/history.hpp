// Civilizations and towns on a generated world (no graphics dependency).
//
// Civilizations start as one town on a random land tile, at least distanceBetweenCivilizations apart.
// Every year each town grows by 10 %; a town founds a daughter town with probability exp(-100 / population),
// 25-50 tiles away in a random direction, on land and away from other towns, and hands over 10-20 % of
// its people. All randomness comes from the world's seeded generator.
#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "world.hpp"

namespace pwg {

enum class Race : uint8_t { Humans, Orcs, Elves, Dwarfs, Halflings };

inline const char *raceName(Race r) {
    static const char *names[] = {"humans", "orcs", "elves", "dwarfs", "halflings"};
    return names[(int)r];
}

struct Town {
    int x = 0, y = 0;
    int civ = 0;
    double population = 0;
    int founded = 0;  // year
    double growth = 1.1;
};

struct Civilization {
    int id = 0;
    Race race = Race::Humans;
    int xStart = 0, yStart = 0;
    std::array<uint8_t, 3> color{}, outline{};
    std::vector<int> towns;  // indices into History::towns

    double population(const std::vector<Town> &all) const {
        double p = 0;
        for (int t : towns) p += all[t].population;
        return p;
    }
};

struct History {
    std::vector<Civilization> civilizations;
    std::vector<Town> towns;
    int year = 0;
};

inline bool landTile(const World &w, int x, int y) {
    if (!w.inside(x, y)) return false;
    const Tile &t = w.at(x, y);
    return !t.isUnderWater && !t.isGlacier;
}

inline bool farFromTowns(const History &h, int x, int y, int minDist) {
    for (const auto &t : h.towns)
        if ((long)(t.x - x) * (t.x - x) + (long)(t.y - y) * (t.y - y) < (long)minDist * minDist) return false;
    return true;
}

inline History spawnCivilizations(World &w) {
    History h;
    const int wanted = std::max(1, (w.X * w.Y) / std::max(1, w.p.density));
    std::uniform_int_distribution<int> px(0, w.X - 1), py(0, w.Y - 1), col(0, 255);
    const long d2 = (long)w.p.distanceBetweenCivilizations * w.p.distanceBetweenCivilizations;
    for (int attempt = 0; (int)h.civilizations.size() < wanted && attempt < 200 * wanted; attempt++) {
        const int x = px(w.rng), y = py(w.rng);
        if (!landTile(w, x, y)) continue;
        bool tooClose = false;
        for (const auto &c : h.civilizations)
            if ((long)(c.xStart - x) * (c.xStart - x) + (long)(c.yStart - y) * (c.yStart - y) < d2) tooClose = true;
        if (tooClose) continue;
        Civilization c;
        c.id = (int)h.civilizations.size();
        c.race = Race(c.id % 5);
        c.xStart = x, c.yStart = y;
        for (auto &v : c.color) v = (uint8_t)col(w.rng);
        for (auto &v : c.outline) v = (uint8_t)col(w.rng);
        Town t;
        t.x = x, t.y = y, t.civ = c.id, t.population = w.p.initialPopulation;
        c.towns.push_back((int)h.towns.size());
        h.towns.push_back(t);
        h.civilizations.push_back(c);
    }
    return h;
}

inline void processYear(World &w, History &h) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::uniform_real_distribution<double> angle(0.0, 2 * 3.14159265358979323846);
    std::uniform_real_distribution<double> dist(25.0, 50.0), share(0.1, 0.2);
    const size_t n = h.towns.size();  // towns founded this year start growing next year
    for (size_t i = 0; i < n; i++) {
        h.towns[i].population *= h.towns[i].growth;
        const double pop = h.towns[i].population;
        if (pop <= 0 || u(w.rng) >= std::exp(-100.0 / pop)) continue;
        for (int attempt = 0; attempt < 12; attempt++) {
            const double a = angle(w.rng), d = dist(w.rng);
            const int x = (int)std::lround(h.towns[i].x + d * std::cos(a));
            const int y = (int)std::lround(h.towns[i].y + d * std::sin(a));
            if (!landTile(w, x, y) || !farFromTowns(h, x, y, w.p.distanceBetweenTowns)) continue;
            Town t;
            t.x = x, t.y = y, t.civ = h.towns[i].civ, t.founded = h.year + 1;
            t.population = std::floor(pop * share(w.rng));
            h.towns[i].population -= t.population;
            h.civilizations[t.civ].towns.push_back((int)h.towns.size());
            h.towns.push_back(t);
            break;
        }
    }
    h.year++;
}

}  // namespace pwg
