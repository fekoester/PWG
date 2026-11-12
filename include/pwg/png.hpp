// Minimal PNG writer for RGBA buffers (uses zlib).
#pragma once

#include <zlib.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pwg {

inline void writePng(const std::string &path, const std::vector<uint8_t> &rgba, int width, int height) {
    if (rgba.size() != (size_t)width * height * 4) throw std::invalid_argument("writePng: buffer size mismatch");
    std::vector<uint8_t> raw;
    raw.reserve((size_t)height * (width * 4 + 1));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgba.begin() + (size_t)y * width * 4, rgba.begin() + (size_t)(y + 1) * width * 4);
    }
    uLongf zlen = compressBound((uLong)raw.size());
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 9) != Z_OK) throw std::runtime_error("zlib failed");
    z.resize(zlen);

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    auto u32 = [](std::vector<uint8_t> &v, uint32_t x) {
        for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
    };
    auto chunk = [&](const char *type, const std::vector<uint8_t> &data) {
        std::vector<uint8_t> c;
        u32(c, (uint32_t)data.size());
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        c.insert(c.end(), body.begin(), body.end());
        u32(c, (uint32_t)crc32(0, body.data(), (uInt)body.size()));
        f.write((const char *)c.data(), (std::streamsize)c.size());
    };
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    f.write((const char *)sig, 8);
    std::vector<uint8_t> ihdr;
    u32(ihdr, (uint32_t)width);
    u32(ihdr, (uint32_t)height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
}

}  // namespace pwg
