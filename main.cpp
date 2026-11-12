// ProcWorld: generate a procedural world, simulate its civilizations, export or view it.
//
//   ProcWorld [layer] [seed] [options]          (positional form of the original: ./ProcWorld full 42)
//   --seed N  --size WxH  --layer NAME  --years N  --export map.png  --no-window  --stats
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>

#include "pwg/history.hpp"
#include "pwg/png.hpp"
#include "pwg/render.hpp"
#include "pwg/world.hpp"

#ifndef PWG_NO_SFML
#include <SFML/Graphics.hpp>
#endif

namespace {

struct Options {
    pwg::Params params;
    pwg::Layer layer = pwg::Layer::Full;
    int years = 1;
    std::string exportPath;
    bool window = true;
    bool stats = false;
};

void usage() {
    std::cout << "usage: ProcWorld [layer] [seed] [--seed N] [--size WxH] [--layer NAME] [--years N]\n"
                 "                 [--export map.png] [--no-window] [--stats]\n"
                 "layers: full height temperature moisture rain wind heat\n"
                 "viewer: 1-7 switch layer, left click = tile info, right click = next year,\n"
                 "        space = new seed, S = save screenshot, Esc = quit\n";
}

bool parse(int argc, char **argv, Options &o) {
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument(a + " needs a value");
            return argv[++i];
        };
        if (a == "-h" || a == "--help") return usage(), false;
        if (a == "--seed") o.params.seed = std::stoi(next());
        else if (a == "--years") o.years = std::stoi(next());
        else if (a == "--export") o.exportPath = next();
        else if (a == "--no-window") o.window = false;
        else if (a == "--stats") o.stats = true;
        else if (a == "--size") {
            std::string s = next();
            auto xpos = s.find('x');
            if (xpos == std::string::npos) throw std::invalid_argument("--size expects WxH");
            o.params.width = std::stoi(s.substr(0, xpos));
            o.params.height = std::stoi(s.substr(xpos + 1));
        } else if (a == "--layer") {
            if (!pwg::parseLayer(next(), o.layer)) throw std::invalid_argument("unknown layer");
        } else if (!a.empty() && a[0] != '-' && positional < 2) {
            // positional layer and/or seed, e.g. "full 42" (the original form), "42" or "height"
            if (a.find_first_not_of("0123456789") == std::string::npos) o.params.seed = std::stoi(a);
            else if (!pwg::parseLayer(a, o.layer)) throw std::invalid_argument("unknown layer '" + a + "'");
            positional++;
        } else {
            throw std::invalid_argument("unknown argument '" + a + "'");
        }
    }
    if (o.years < 0) throw std::invalid_argument("--years must be >= 0");
    return true;
}

struct Sim {
    pwg::World world;
    pwg::History history;
};

std::unique_ptr<Sim> build(const pwg::Params &p, int years) {
    auto s = std::unique_ptr<Sim>(new Sim{pwg::generateWorld(p), {}});
    s->history = pwg::spawnCivilizations(s->world);
    for (int y = 0; y < years; y++) pwg::processYear(s->world, s->history);
    return s;
}

void printStats(const Sim &s) {
    std::map<pwg::Biome, size_t> count;
    for (const auto &t : s.world.tiles) count[t.biome()]++;
    std::printf("seed %d, %dx%d, year %d\n", s.world.p.seed, s.world.X, s.world.Y, s.history.year);
    for (const auto &[b, n] : count)
        std::printf("  %-11s %6.2f %%\n", pwg::biomeName(b), 100.0 * n / s.world.tiles.size());
    std::printf("  rivers %zu, civilizations %zu, towns %zu\n", s.world.riverSources.size(),
                s.history.civilizations.size(), s.history.towns.size());
    for (const auto &c : s.history.civilizations)
        std::printf("  civ %2d %-9s at (%4d,%4d): %zu towns, population %.0f\n", c.id, pwg::raceName(c.race),
                    c.xStart, c.yStart, c.towns.size(), c.population(s.history.towns));
}

[[maybe_unused]] void printTile(const Sim &s, int x, int y) {
    if (!s.world.inside(x, y)) return;
    const auto &t = s.world.at(x, y);
    std::printf("(%d,%d) %s  height %.3f  temperature %.3f  moisture %.3f  rain %.3f  wind %.3f  heat %.3f\n", x, y,
                pwg::biomeName(t.biome()), t.height, t.temperature, t.moisture, t.rain, t.wind, t.heat);
    for (const auto &town : s.history.towns)
        if (std::abs(town.x - x) <= 3 && std::abs(town.y - y) <= 3) {
            const auto &c = s.history.civilizations[town.civ];
            std::printf("  town of civilization %d (%s), population %.0f, founded in year %d\n", c.id,
                        pwg::raceName(c.race), town.population, town.founded);
        }
}

#ifndef PWG_NO_SFML
// ---- SFML 2 / 3 compatibility -------------------------------------------------------------------
#if SFML_VERSION_MAJOR >= 3
using Key = sf::Keyboard::Key;
std::unique_ptr<sf::RenderWindow> makeWindow(unsigned w, unsigned h, const std::string &title) {
    return std::make_unique<sf::RenderWindow>(sf::VideoMode({w, h}), title);
}
void upload(sf::Texture &tex, const std::vector<uint8_t> &px, unsigned w, unsigned h) {
    if (tex.getSize() != sf::Vector2u{w, h}) (void)tex.resize({w, h});
    tex.update(px.data());
}
#else
namespace KeyNS {
enum Key { Num1 = sf::Keyboard::Num1, Space = sf::Keyboard::Space, S = sf::Keyboard::S, Escape = sf::Keyboard::Escape };
}
std::unique_ptr<sf::RenderWindow> makeWindow(unsigned w, unsigned h, const std::string &title) {
    return std::make_unique<sf::RenderWindow>(sf::VideoMode(w, h), title);
}
void upload(sf::Texture &tex, const std::vector<uint8_t> &px, unsigned w, unsigned h) {
    if (tex.getSize() != sf::Vector2u(w, h)) tex.create(w, h);
    tex.update(px.data());
}
#endif

void view(std::unique_ptr<Sim> sim, pwg::Layer layer, int years) {
    const unsigned W = (unsigned)sim->world.X, H = (unsigned)sim->world.Y;
    auto title = [&]() {
        return "ProcWorld - seed " + std::to_string(sim->world.p.seed) + " - " + pwg::layerNames()[(int)layer] +
               " - year " + std::to_string(sim->history.year);
    };
    auto window = makeWindow(W, H, title());
    window->setFramerateLimit(30);
    sf::Texture tex;
    bool dirty = true;
    std::mt19937 seeds(std::random_device{}());

    auto onKey = [&](int code) {
#if SFML_VERSION_MAJOR >= 3
        const int num1 = (int)Key::Num1, space = (int)Key::Space, s = (int)Key::S, esc = (int)Key::Escape;
#else
        const int num1 = KeyNS::Num1, space = KeyNS::Space, s = KeyNS::S, esc = KeyNS::Escape;
#endif
        if (code >= num1 && code < num1 + 7) layer = pwg::Layer(code - num1), dirty = true;
        else if (code == space) {
            pwg::Params p = sim->world.p;
            p.seed = (int)(seeds() % 1000000);
            sim = build(p, years);
            dirty = true;
        } else if (code == s) {
            const std::string path = "world_" + std::to_string(sim->world.p.seed) + "_" +
                                     pwg::layerNames()[(int)layer] + ".png";
            pwg::writePng(path, pwg::render(sim->world, &sim->history, layer), sim->world.X, sim->world.Y);
            std::printf("saved %s\n", path.c_str());
        } else if (code == esc) window->close();
    };
    auto onClick = [&](bool left, int x, int y) {
        if (left) printTile(*sim, x, y);
        else pwg::processYear(sim->world, sim->history), dirty = true;  // once per press, not while held
    };

    while (window->isOpen()) {
#if SFML_VERSION_MAJOR >= 3
        while (const std::optional event = window->pollEvent()) {
            if (event->is<sf::Event::Closed>()) window->close();
            else if (const auto *k = event->getIf<sf::Event::KeyPressed>()) onKey((int)k->code);
            else if (const auto *m = event->getIf<sf::Event::MouseButtonPressed>())
                onClick(m->button == sf::Mouse::Button::Left, m->position.x, m->position.y);
        }
#else
        sf::Event event;
        while (window->pollEvent(event)) {
            if (event.type == sf::Event::Closed) window->close();
            else if (event.type == sf::Event::KeyPressed) onKey((int)event.key.code);
            else if (event.type == sf::Event::MouseButtonPressed)
                onClick(event.mouseButton.button == sf::Mouse::Left, event.mouseButton.x, event.mouseButton.y);
        }
#endif
        if (!window->isOpen()) break;
        if (dirty) {
            upload(tex, pwg::render(sim->world, &sim->history, layer), W, H);
            window->setTitle(title());
            dirty = false;
        }
        window->clear();
        sf::Sprite sprite(tex);
        window->draw(sprite);
        window->display();
    }
}
#endif

}  // namespace

int main(int argc, char **argv) {
    Options o;
    try {
        if (!parse(argc, argv, o)) return 0;
        auto sim = build(o.params, o.years);
        if (o.stats) printStats(*sim);
        if (!o.exportPath.empty()) {
            pwg::writePng(o.exportPath, pwg::render(sim->world, &sim->history, o.layer), sim->world.X, sim->world.Y);
            std::printf("wrote %s\n", o.exportPath.c_str());
        }
        if (o.window) {
#ifdef PWG_NO_SFML
            std::fprintf(stderr, "built without SFML: use --export / --stats (--no-window)\n");
            return o.exportPath.empty() && !o.stats ? 1 : 0;
#else
            view(std::move(sim), o.layer, o.years);
#endif
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        usage();
        return 2;
    }
    return 0;
}
