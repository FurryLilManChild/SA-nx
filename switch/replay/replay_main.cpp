// SA-nx capture replayer: draws F11 captures (character select, Emerald Coast) through the game's
// own renderer (Offscreen + Renderer, as rcap does) on the Switch, once per experiment, so the
// slow per-pixel pass of the menus can be pinned on one difference from the fast probe. Writes
// sdmc:/switch/sonicadv/replay.txt. On Linux: replay_linux DIR (capture-NNN.* files).
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "dream/render/background.h"
#include "dream/render/display_list.h"
#include "dream/render/vk/context.h"
#include "dream/render/vk/offscreen.h"
#include "dream/render/vk/renderer.h"

using namespace dream::render;

namespace {

FILE* g_log = nullptr;

void log(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    va_end(a);
    if (g_log) {
        va_start(a, fmt);
        std::vfprintf(g_log, fmt, a);
        va_end(a);
        std::fflush(g_log);
    }
}

std::vector<char> read_file(const std::string& path) {
    std::vector<char> out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        out.resize(static_cast<std::size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(out.data(), 1, out.size(), f) != out.size())
            out.clear();
        std::fclose(f);
    }
    return out;
}

struct Capture {
    std::string name;
    std::vector<char> ta, vram;
    std::vector<std::uint32_t> regs;
};

struct Experiment {
    const char* name;
    const char* what;
    bool oit = true, lean = false, triangles = false;
    vk::Offscreen::Experiment e{};
};

std::uint32_t checksum(const std::uint32_t* px, std::size_t n) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}

#ifdef __SWITCH__
struct Clock {
    PcvModuleId module;
    std::uint32_t hz;
    ClkrstSession session{};
    std::uint32_t old_hz = 0;
    bool open = false;
};
Clock g_clocks[3] = {{PcvModuleId_CpuBus, 1785000000u},
                     {PcvModuleId_GPU, 768000000u},
                     {PcvModuleId_EMC, 1600000000u}};
void clocks_up() {
    if (R_FAILED(clkrstInitialize()))
        return;
    for (Clock& c : g_clocks)
        if (R_SUCCEEDED(clkrstOpenSession(&c.session, c.module, 3))) {
            c.open = true;
            clkrstGetClockRate(&c.session, &c.old_hz);
            clkrstSetClockRate(&c.session, c.hz);
        }
}
void clocks_down() {
    for (Clock& c : g_clocks)
        if (c.open) {
            clkrstSetClockRate(&c.session, c.old_hz);
            clkrstCloseSession(&c.session);
        }
    clkrstExit();
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef __SWITCH__
    (void)argc;
    (void)argv;
    const std::string dir = "sdmc:/switch/sonicadv/replay/";
    g_log = std::fopen("sdmc:/switch/sonicadv/replay.txt", "w");
    clocks_up();
#else
    const std::string dir = argc > 1 ? std::string(argv[1]) + "/" : "./";
#endif
    log("SA-nx capture replayer\n");
    std::vector<Capture> captures;
    for (const char* n : {"013", "015", "002"}) {
        Capture c;
        c.name = std::string("capture-") + n;
        const std::string stem = dir + c.name;
        c.ta = read_file(stem + ".ta");
        c.vram = read_file(stem + ".vram");
        const std::vector<char> regs = read_file(stem + ".vram.regs");
        if (c.ta.empty() || c.vram.empty() || regs.size() < 8192) {
            log("missing %s (.ta, .vram, .vram.regs)\n", stem.c_str());
            continue;
        }
        c.regs.resize(regs.size() / 4);
        std::memcpy(c.regs.data(), regs.data(), regs.size());
        captures.push_back(std::move(c));
    }

    std::vector<Experiment> experiments;
    auto add = [&](const char* name, const char* what, auto set) {
        Experiment x{name, what};
        set(x);
        experiments.push_back(x);
    };
    // Cheapest first, so a run that ends early still has them.
    add("strip", "per-strip sort, no per-pixel pass", [](Experiment& x) { x.oit = false; });
    add("triangle", "per-triangle sort", [](Experiment& x) {
        x.oit = false;
        x.triangles = true;
    });
    add("resolveonly", "resolve over empty lists", [](Experiment& x) { x.e.skip_gather = true; });
    add("gatheronly", "gather, no resolve", [](Experiment& x) { x.e.skip_resolve = true; });
    add("split", "resolve in its own pass instance", [](Experiment& x) { x.e.split = true; });
    add("boxscissor", "resolve scissored to the translucent box (before the fix)",
        [](Experiment& x) { x.e.box_scissor = true; });
    add("nostamps", "no timestamp between gather and resolve",
        [](Experiment& x) { x.e.no_mid_stamps = true; });
    add("lean", "lean (8 kept)", [](Experiment& x) { x.lean = true; });
    add("game", "as the game draws it (accurate)", [](Experiment&) {});

    vk::Context ctx;
    if (!ctx.create({}, false)) {
        log("context: %s\n", ctx.error().c_str());
        return 1;
    }
    log("device: %s\n", ctx.caps().device_name.c_str());
    for (const Capture& c : captures) {
        // A fresh offscreen target and renderer per capture, as at the game's start.
        vk::Offscreen off;
        vk::Renderer r;
        if (!off.create(ctx, 640, 480) || !r.create(ctx, off.render_pass())) {
            log("%s: cannot create the renderer: %s %s\n", c.name.c_str(), off.error().c_str(),
                r.error().c_str());
            continue;
        }
        const auto pf = static_cast<PaletteFormat>(c.regs[0x108 / 4] & 3);
        r.set_memory(reinterpret_cast<const std::uint8_t*>(c.vram.data()), c.vram.size(),
                     c.regs.data() + 0x1000 / 4, pf);
        r.textures().set_stride_width((c.regs[0xE4 / 4] & 31u) * 32u);
        DisplayList dl;
        dl.feed_stream(reinterpret_cast<const std::uint32_t*>(c.ta.data()), c.ta.size() / 4);
        add_background(dl.frame(), c.regs.data(), reinterpret_cast<const std::uint8_t*>(c.vram.data()),
                       c.vram.size());
        vk::FrameGeometry g;
        g.alpha_ref = static_cast<float>(c.regs[0x11C / 4] & 0xFFu) / 255.0f;
        off.readback = false;
        off.finish_timeout_ns = 30'000'000'000ull;  // measure a slow experiment, do not drop it
        log("\n%s: %zu translucent polygons\n", c.name.c_str(), dl.frame().lists[2].size());
        log("%-12s %9s %9s %9s %9s %9s  %-9s %s\n", "experiment", "wall ms", "first", "gather",
            "resolve", "total", "picture", "what");
        for (const Experiment& x : experiments) {
            off.allow_oit = x.oit;
            off.lean_oit = x.lean;
            off.experiment = x.e;
            r.sort_triangles = x.triangles;
            std::vector<double> wall;
            double first = 0;
            auto gpu0 = off.gpu;
            bool failed = false;
            for (int rep = 0; rep < 6; ++rep) {
                if (rep == 1)
                    gpu0 = off.gpu;  // the GPU times leave out the first render
                const auto t0 = std::chrono::steady_clock::now();
                if (!off.render(r, dl.frame(), g) || !off.finish()) {
                    log("%-12s render failed: %s\n", x.name, off.error().c_str());
                    failed = true;
                    break;
                }
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0)
                                      .count();
                if (rep == 0)
                    first = ms;
                else
                    wall.push_back(ms);
                if (ms > 400.0 && rep >= 2)
                    break;  // slow: three samples are enough
            }
            if (failed)
                break;  // the device may be lost
            const auto& gp = off.gpu;
            const double n = static_cast<double>(std::max<std::uint64_t>(gp.frames - gpu0.frames, 1));
            const auto avg = [n](std::uint64_t a, std::uint64_t b) {
                return static_cast<double>(a - b) / n / 1e6;
            };
            std::sort(wall.begin(), wall.end());
            const std::uint32_t* px = off.pixels();
            char sum[16] = "-";
            if (px)
                std::snprintf(sum, sizeof sum, "%08x", checksum(px, 640u * 480u));
            log("%-12s %9.2f %9.2f %9.2f %9.2f %9.2f  %-9s %s\n", x.name,
                wall.empty() ? first : wall[wall.size() / 2], first,
                avg(gp.gather_ns, gpu0.gather_ns), avg(gp.resolve_ns, gpu0.resolve_ns),
                avg(gp.total_ns, gpu0.total_ns), sum, x.what);
        }
        log("lists: %u fragments at most, longest %u, unterminated %llu\n", off.stats.fragments_max,
            off.stats.longest_walk, static_cast<unsigned long long>(off.stats.unterminated));
    }
    log("\ndone\n");
#ifdef __SWITCH__
    clocks_down();
    if (g_log)
        std::fclose(g_log);
    g_log = nullptr;
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    std::printf("SA-nx capture replayer: done.\n\nResults are in /switch/sonicadv/replay.txt\n\n"
                "Press + to return to the homebrew menu.\n");
    consoleUpdate(nullptr);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
#endif
    return 0;
}
