// Map layers rendered into an RGBA pixel buffer (no graphics dependency).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "history.hpp"
#include "world.hpp"

namespace pwg {

enum class Layer { Full, Height, Temperature, Moisture, Rain, Wind, Heat };

inline const std::vector<std::string> &layerNames() {
    static const std::vector<std::string> n = {"full", "height", "temperature", "moisture", "rain", "wind", "heat"};
    return n;
}

inline bool parseLayer(const std::string &s, Layer &out) {
    const auto &n = layerNames();
    for (size_t i = 0; i < n.size(); i++)
        if (n[i] == s) return out = Layer(i), true;
    return false;
}

inline std::array<uint8_t, 3> biomeColor(Biome b) {
    switch (b) {
        case Biome::Ocean: return {0, 0, 255};
        case Biome::FreshWater: return {40, 110, 255};
        case Biome::Glacier: return {255, 255, 255};
        case Biome::Snow: return {188, 188, 188};
        case Biome::Mountain: return {100, 75, 0};
        case Biome::Forest: return {0, 75, 0};
        case Biome::Grassland: return {0, 255, 0};
        case Biome::Desert: return {255, 255, 0};
    }
    return {217, 124, 114};
}

// RGBA pixels, width X, height Y, row-major (y rows of x pixels), opaque.
inline std::vector<uint8_t> render(const World &w, const History *h, Layer layer) {
    std::vector<uint8_t> px((size_t)w.X * w.Y * 4, 255);
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (!w.inside(x, y)) return;
        uint8_t *p = &px[((size_t)y * w.X + x) * 4];
        p[0] = r, p[1] = g, p[2] = b, p[3] = 255;
    };
    for (const auto &t : w.tiles) {
        if (layer == Layer::Full) {
            auto c = biomeColor(t.biome());
            const double a = t.opacity / 255.0;  // shading over black, as the original alpha blending
            put(t.x, t.y, (uint8_t)(c[0] * a), (uint8_t)(c[1] * a), (uint8_t)(c[2] * a));
        } else {
            double v = 0;
            switch (layer) {
                case Layer::Height: v = t.height; break;
                case Layer::Temperature: v = t.temperature; break;
                case Layer::Moisture: v = t.moisture; break;
                case Layer::Rain: v = t.rain; break;
                case Layer::Wind: v = t.wind; break;
                case Layer::Heat: v = t.heat; break;
                default: break;
            }
            const auto g = (uint8_t)std::lround(255 * std::min(1.0, std::max(0.0, v)));
            put(t.x, t.y, g, g, g);
        }
    }
    if (h && layer == Layer::Full) {
        for (const auto &t : h->towns) {
            const auto &c = h->civilizations[t.civ];
            for (int dx = -3; dx <= 3; dx++)
                for (int dy = -3; dy <= 3; dy++) {
                    const bool edge = std::abs(dx) == 3 || std::abs(dy) == 3;
                    const auto &col = edge ? c.outline : c.color;
                    put(t.x + dx, t.y + dy, col[0], col[1], col[2]);
                }
        }
    }
    return px;
}

}  // namespace pwg
