// Procedural world generation (no graphics dependency).
//
// Pipeline (generate()): height map (fractal Perlin noise, sunk towards the map edge) -> mountains (top of the
// height range) -> oceans (below the water level) -> rivers (from mountain sources down to the sea) -> wind,
// heat, rain -> temperature -> moisture (rivers moisten their surroundings) -> forests / grassland ->
// deserts -> snow -> glaciers. Every random choice comes from one generator seeded with Params::seed, so a
// seed always produces the same world.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "FastNoise.h"

namespace pwg {

struct Params {
    int width = 1024;
    int height = 780;
    int seed = 1;
    double edgeOfWorld = 0.2;              // fraction of the map over which land sinks towards the edge
    double waterLevelPercentage = 0.6;     // oceans below this fraction of the height range
    double mountainLevelPercentage = 0.80; // mountains above this fraction of the height range
    double forestPercentage = 0.12;        // share of the non-mountain land that is forest (the wettest part)
    double edgeTemperatureMap = 0.25;
    double isDesertMoisture = 0.15;
    double isDesertTemperature = 0.2;
    double isSnowTemperature = 0.25;
    double isGlacierTemperature = 0.10;
    int distanceBetweenRivers = 16;        // minimum distance between river sources (tiles)
    int riverMoistureRadius = 15;
    // civilizations
    int density = 25000;                   // one civilization per this many tiles
    int distanceBetweenCivilizations = 35;
    int initialPopulation = 20;
    int distanceBetweenTowns = 15;
};

enum class Biome : uint8_t { Ocean, FreshWater, Glacier, Snow, Mountain, Forest, Grassland, Desert };

struct Tile {
    int x = 0, y = 0;
    double height = 0, wind = 0, heat = 0, temperature = 0, moisture = 0, rain = 0;
    bool isUnderWater = false, isOcean = false, isRiver = false;
    bool isMountain = false, isForest = false, isHighland = false, isDesert = false;
    bool hasSnow = false, isGlacier = false;
    int opacity = 200;

    // Land type: exactly one of desert / forest / highland (grassland) / mountain for dry land.
    int landTypes() const { return isDesert + isForest + isHighland + isMountain; }

    // Display priority of the original program: glacier > water > snow > grassland > mountain > forest > desert.
    Biome biome() const {
        if (isGlacier) return Biome::Glacier;
        if (isUnderWater) return isOcean ? Biome::Ocean : Biome::FreshWater;
        if (hasSnow) return Biome::Snow;
        if (isHighland) return Biome::Grassland;
        if (isMountain) return Biome::Mountain;
        if (isForest) return Biome::Forest;
        return Biome::Desert;
    }
};

inline const char *biomeName(Biome b) {
    switch (b) {
        case Biome::Ocean: return "ocean";
        case Biome::FreshWater: return "river/lake";
        case Biome::Glacier: return "glacier";
        case Biome::Snow: return "snow";
        case Biome::Mountain: return "mountain";
        case Biome::Forest: return "forest";
        case Biome::Grassland: return "grassland";
        case Biome::Desert: return "desert";
    }
    return "?";
}

struct World {
    Params p;
    int X, Y;
    std::vector<Tile> tiles;            // row-major by x: tiles[x * Y + y]
    std::vector<int> mountains;         // tile indices
    std::vector<int> riverSources;      // tile indices
    double waterLevel = 0, mountainLevel = 0;
    double landFraction = 0, oceanFraction = 0;
    std::mt19937_64 rng;

    explicit World(const Params &params) : p(params), X(params.width), Y(params.height), rng((uint64_t)params.seed) {
        if (X < 20 || Y < 20) throw std::invalid_argument("the map must be at least 20 x 20 tiles");
        tiles.resize((size_t)X * Y);
        for (int x = 0; x < X; x++)
            for (int y = 0; y < Y; y++) at(x, y).x = x, at(x, y).y = y;
    }

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < X && y < Y; }
    Tile &at(int x, int y) { return tiles[(size_t)x * Y + y]; }
    const Tile &at(int x, int y) const { return tiles[(size_t)x * Y + y]; }
    int index(int x, int y) const { return x * Y + y; }
};

namespace detail {

// Rescale one field of every tile to [0, 1].
template <class Get>
void normalise(World &w, Get get) {
    double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
    for (auto &t : w.tiles) lo = std::min(lo, get(t)), hi = std::max(hi, get(t));
    const double span = hi > lo ? hi - lo : 1.0;
    for (auto &t : w.tiles) get(t) = (get(t) - lo) / span;
}

inline FastNoise noise(int seed, float freq, int octaves, FastNoise::NoiseType type, float perturb = 0) {
    FastNoise n;
    n.SetSeed(seed);
    n.SetFrequency(freq);
    n.SetFractalOctaves(octaves);
    n.SetNoiseType(type);
    if (perturb > 0) n.SetGradientPerturbAmp(perturb);
    return n;
}

// Perturbed noise field, as in the original wind / heat / rain maps.
template <class Get>
void perturbedField(World &w, FastNoise n, Get get) {
    for (auto &t : w.tiles) {
        float fx = (float)t.x, fy = (float)t.y;
        n.GradientPerturbFractal(fx, fy);
        get(t) = n.GetNoise(fx, fy);
    }
    normalise(w, get);
}

}  // namespace detail

inline void heightMap(World &w) {
    auto n = detail::noise(w.p.seed, 0.01f, 6, FastNoise::PerlinFractal);
    double hi = std::numeric_limits<double>::lowest();
    for (auto &t : w.tiles) t.height = n.GetNoise((float)t.x, (float)t.y), hi = std::max(hi, t.height);
    // sink the land towards the edge of the map
    const double e = w.p.edgeOfWorld;
    for (auto &t : w.tiles) {
        if (t.x < w.X * e) t.height -= (w.X * e - t.x) / (w.X * e) * hi;
        if (t.x > w.X * (1 - e)) t.height -= (t.x - w.X * (1 - e)) / (w.X * e) * hi;
        if (t.y > w.Y * (1 - e)) t.height -= (t.y - w.Y * (1 - e)) / (w.Y * e) * hi;
        if (t.y < w.Y * e) t.height -= (w.Y * e - t.y) / (w.Y * e) * hi;
    }
    detail::normalise(w, [](Tile &t) -> double & { return t.height; });
}

inline void mountainsAndOceans(World &w) {
    w.mountainLevel = w.p.mountainLevelPercentage;  // heights are normalised to [0, 1]
    w.waterLevel = w.p.waterLevelPercentage;
    w.mountains.clear();
    size_t ocean = 0;
    for (auto &t : w.tiles) {
        if (t.height > w.mountainLevel) t.isMountain = true, w.mountains.push_back(w.index(t.x, t.y));
        if (t.height < w.waterLevel) t.isUnderWater = t.isOcean = true, ocean++;
    }
    w.oceanFraction = double(ocean) / w.tiles.size();
    w.landFraction = 1.0 - w.oceanFraction;
    // shading: brighter land and darker deep sea
    for (auto &t : w.tiles) {
        if (t.height > w.waterLevel)
            t.opacity = (int)(255 * (1.0 - 0.65 * (t.height - w.waterLevel) / (1.0 - w.waterLevel)));
        else
            t.opacity = (int)(255 * (1.0 - 0.85 * (w.waterLevel - t.height) / w.waterLevel));
    }
}

// Rivers start at random mountain tiles at least distanceBetweenRivers apart and step to the lowest
// neighbour that is lower than the source and not yet water, until they reach the sea or get stuck
// (a lake). Comparing with the source (not the current tile) lets a river cross small dips.
inline void rivers(World &w) {
    w.riverSources.clear();
    if (w.mountains.empty()) return;
    const double m = (double)w.mountains.size() / 100.0;
    std::uniform_int_distribution<int> howMany((int)(m * 0.4) + 2, (int)(m * 0.6) + 5);
    const int wanted = howMany(w.rng);
    std::uniform_int_distribution<size_t> pick(0, w.mountains.size() - 1);
    const long d2min = (long)w.p.distanceBetweenRivers * w.p.distanceBetweenRivers;
    for (int attempt = 0; (int)w.riverSources.size() < wanted && attempt < 50 * wanted; attempt++) {
        const Tile &c = w.tiles[w.mountains[pick(w.rng)]];
        bool tooClose = false;
        for (int s : w.riverSources) {
            const Tile &o = w.tiles[s];
            if ((long)(o.x - c.x) * (o.x - c.x) + (long)(o.y - c.y) * (o.y - c.y) < d2min) tooClose = true;
        }
        if (!tooClose) w.riverSources.push_back(w.index(c.x, c.y));
    }
    for (int s : w.riverSources) {
        const double sourceHeight = w.tiles[s].height;
        int x = w.tiles[s].x, y = w.tiles[s].y;
        w.at(x, y).isUnderWater = w.at(x, y).isRiver = true;
        for (int len = 0; len < w.X * w.Y; len++) {
            bool sea = false;
            int bx = 0, by = 0;
            double best = std::numeric_limits<double>::max();
            for (int dx = -1; dx <= 1 && !sea; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if ((dx == 0 && dy == 0) || !w.inside(x + dx, y + dy)) continue;
                    const Tile &n = w.at(x + dx, y + dy);
                    if (n.isOcean) { sea = true; break; }
                    if (!n.isUnderWater && n.height < best && n.height < sourceHeight)
                        best = n.height, bx = dx, by = dy;
                }
            }
            if (sea || (bx == 0 && by == 0)) break;
            x += bx, y += by;
            w.at(x, y).isUnderWater = w.at(x, y).isRiver = true;
        }
    }
}

inline void climate(World &w) {
    using FN = FastNoise;
    detail::perturbedField(w, detail::noise(w.p.seed, 0.015f, 2, FN::Value, 45), [](Tile &t) -> double & { return t.wind; });
    detail::perturbedField(w, detail::noise(w.p.seed, 0.004f, 3, FN::Value, 35), [](Tile &t) -> double & { return t.heat; });
    detail::perturbedField(w, detail::noise(w.p.seed, 0.03f, 2, FN::Value, 20), [](Tile &t) -> double & { return t.rain; });
    for (auto &t : w.tiles) {
        t.temperature = t.heat - std::fabs(t.wind) * 0.15 - std::fabs(t.height) * 0.75 - std::fabs(t.rain) * 0.2;
        if (t.isOcean) t.temperature -= 0.5;
    }
    // colder towards the top of the map (perturbed)
    FastNoise n = detail::noise(w.p.seed, 0.004f, 3, FastNoise::Value, 35);
    for (auto &t : w.tiles) {
        float fx = (float)t.x, fy = (float)t.y;
        n.GradientPerturbFractal(fx, fy);
        const double tmp = n.GetNoise(fx, fy) + 1.0 - 0.35 * (w.Y - t.y) / (w.Y * w.p.edgeTemperatureMap);
        if (tmp > 0) t.temperature -= tmp;
    }
    detail::normalise(w, [](Tile &t) -> double & { return t.temperature; });
}

inline void moisture(World &w) {
    for (auto &t : w.tiles) t.moisture = -t.temperature - t.height + 2.0 * t.rain;
    detail::normalise(w, [](Tile &t) -> double & { return t.moisture; });
    // fresh water moistens the land around it
    const int R = w.p.riverMoistureRadius;
    std::vector<double> add(w.tiles.size(), 0.0);
    for (const auto &t : w.tiles) {
        if (!(t.isUnderWater && !t.isOcean)) continue;
        for (int dx = -R; dx <= R; dx++)
            for (int dy = -R; dy <= R; dy++)
                if (w.inside(t.x + dx, t.y + dy)) add[w.index(t.x + dx, t.y + dy)] += 0.001;
    }
    for (size_t i = 0; i < w.tiles.size(); i++) w.tiles[i].moisture += add[i];
    detail::normalise(w, [](Tile &t) -> double & { return t.moisture; });
    for (auto &t : w.tiles)
        if (t.isOcean) t.moisture = 0.0;
}

// The wettest `forestPercentage` of the dry, non-mountain land becomes forest, the rest grassland;
// dry and warm grassland becomes desert.
inline void vegetation(World &w) {
    std::vector<double> m;
    for (const auto &t : w.tiles)
        if (!t.isMountain && !t.isUnderWater) m.push_back(t.moisture);
    double threshold = std::numeric_limits<double>::max();
    if (!m.empty() && w.p.forestPercentage > 0) {
        const size_t k = (size_t)std::floor((1.0 - std::min(1.0, w.p.forestPercentage)) * (m.size() - 1));
        std::nth_element(m.begin(), m.begin() + k, m.end());
        threshold = m[k];
    }
    for (auto &t : w.tiles) {
        if (t.isMountain || t.isUnderWater) continue;
        if (t.moisture > threshold) t.isForest = true;
        else if (t.moisture < w.p.isDesertMoisture && t.temperature > w.p.isDesertTemperature) t.isDesert = true;
        else t.isHighland = true;
    }
}

inline void coldRegions(World &w) {
    for (auto &t : w.tiles) {
        if (!t.isUnderWater && t.temperature < w.p.isSnowTemperature) t.hasSnow = true;
        if (t.temperature < w.p.isGlacierTemperature) t.isGlacier = true;  // includes sea ice
    }
}

inline void generate(World &w) {
    heightMap(w);
    mountainsAndOceans(w);
    rivers(w);
    climate(w);
    moisture(w);
    vegetation(w);
    coldRegions(w);
}

inline World generateWorld(const Params &p) {
    World w(p);
    generate(w);
    return w;
}

}  // namespace pwg
