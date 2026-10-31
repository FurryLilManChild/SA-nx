// SA-nx resolve probe: loads the per-pixel lists (two real frames written by rcap from Mark's
// captures, and three made-up patterns), runs them through deko3d and then NVK, and writes
// sdmc:/switch/sonicadv/probe.txt. On Linux: probe_linux DIR (the .bin files) runs the Vulkan side.
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "probe.h"

namespace probe {
namespace {
FILE* g_log = nullptr;
}

void log(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    va_end(a);
    if (g_log) {
        va_start(a, fmt);
        std::vfprintf(g_log, fmt, a);
        va_end(a);
        std::fflush(g_log);  // a crash in the next API keeps what was measured
    }
}

std::uint32_t checksum(const std::uint32_t* px, std::size_t n) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}

namespace {

// A list file from rcap (RCAP_DUMP_OIT): "SAOIT1\0\0", width, height, node count, heads, nodes.
bool load(const std::string& path, const char* name, Scenario& s) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        log("missing %s\n", path.c_str());
        return false;
    }
    char magic[8];
    std::uint32_t hdr[3];
    bool ok = std::fread(magic, 1, 8, f) == 8 && !std::memcmp(magic, "SAOIT1", 6) &&
              std::fread(hdr, 4, 3, f) == 3 && hdr[0] && hdr[1] && hdr[0] <= 4096 && hdr[1] <= 4096;
    if (ok) {
        s.name = name;
        s.width = hdr[0];
        s.height = hdr[1];
        s.heads.resize(std::size_t{hdr[0]} * hdr[1]);
        s.nodes.resize(std::size_t{hdr[2]} * 4);
        ok = std::fread(s.heads.data(), 4, s.heads.size(), f) == s.heads.size() &&
             std::fread(s.nodes.data(), 4, s.nodes.size(), f) == s.nodes.size();
    }
    std::fclose(f);
    if (!ok)
        log("unreadable %s\n", path.c_str());
    return ok;
}

// Every pixel in rows 40 to 439 (83% of the screen, like character select) gets `layers`
// fragments with made-up colours and depths, blended "source alpha, one minus source alpha".
Scenario synthetic(std::uint32_t layers) {
    Scenario s;
    s.name = "even_" + std::to_string(layers) + "_layers";
    s.width = 640;
    s.height = 480;
    s.heads.assign(std::size_t{640} * 480, kEnd);
    std::uint32_t rng = 12345u, order = 0;
    auto next = [&rng] {
        rng = rng * 1664525u + 1013904223u;
        return rng;
    };
    for (std::uint32_t y = 40; y < 440; ++y)
        for (std::uint32_t x = 0; x < 640; ++x)
            for (std::uint32_t l = 0; l < layers; ++l) {
                const std::uint32_t i = s.count();
                const std::uint32_t colour = (next() & 0x00FFFFFFu) | 0x80000000u;
                const float depth = static_cast<float>(next() >> 8) / 16777216.0f;
                std::uint32_t dbits;
                std::memcpy(&dbits, &depth, 4);
                const std::uint32_t info = 4u | (5u << 3) | ((order++ & 0x3FFFFFFu) << 6);
                std::uint32_t& head = s.heads[std::size_t{y} * 640 + x];
                s.nodes.insert(s.nodes.end(), {colour, dbits, info, head});
                head = i;
            }
    return s;
}

#ifdef __SWITCH__
// The game's clocks while Mark plays (CPU 1785, GPU 768, memory 1600 MHz), so the times compare
// with the game's log. Restored at exit.
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
            std::uint32_t now = 0;
            clkrstGetClockRate(&c.session, &now);
            log("clock %d: %u -> %u MHz\n", int(c.module), c.old_hz / 1000000u, now / 1000000u);
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

void summary(const std::vector<Timing>& all) {
    log("\nsummary (median wall ms per resolve):\n%-14s %-12s %10s %10s %s\n", "scenario", "variant",
        "deko3d", "nvk", "same picture");
    std::vector<std::pair<std::string, std::string>> keys;
    for (const Timing& t : all)
        if (std::find(keys.begin(), keys.end(), std::make_pair(t.scenario, t.variant)) == keys.end())
            keys.push_back({t.scenario, t.variant});
    for (const auto& [sc, va] : keys) {
        const Timing* dk = nullptr;
        const Timing* nv = nullptr;
        for (const Timing& t : all)
            if (t.scenario == sc && t.variant == va)
                (t.api == "deko3d" ? dk : nv) = &t;
        char a[32] = "-", b[32] = "-";
        if (dk && dk->ok)
            std::snprintf(a, sizeof a, "%.2f", dk->median_ms);
        if (nv && nv->ok)
            std::snprintf(b, sizeof b, "%.2f", nv->median_ms);
        log("%-14s %-12s %10s %10s %s\n", sc.c_str(), va.c_str(), a, b,
            dk && nv && dk->ok && nv->ok ? (dk->checksum == nv->checksum ? "yes" : "NO") : "");
    }
}

}  // namespace
}  // namespace probe

using namespace probe;

int main(int argc, char** argv) {
#ifdef __SWITCH__
    (void)argc;
    (void)argv;
    const std::string dir = "sdmc:/switch/sonicadv/probe/";
    g_log = std::fopen("sdmc:/switch/sonicadv/probe.txt", "w");
#else
    const std::string dir = argc > 1 ? std::string(argv[1]) + "/" : "./";
#endif
    log("SA-nx resolve probe\n");
    std::vector<Scenario> scenarios;
    Scenario s;
    if (load(dir + "oit-013.bin", "char_select", s))
        scenarios.push_back(std::move(s));
    if (load(dir + "oit-002.bin", "emerald_coast", s))
        scenarios.push_back(std::move(s));
    for (std::uint32_t layers : {1u, 2u, 4u})
        scenarios.push_back(synthetic(layers));
    for (const Scenario& sc : scenarios) {
        std::uint32_t covered = 0;
        for (std::uint32_t h : sc.heads)
            covered += h != kEnd;
        log("scenario %-14s %ux%u, %u fragments over %u pixels\n", sc.name.c_str(), sc.width,
            sc.height, sc.count(), covered);
    }

    std::vector<Timing> all;
#ifdef __SWITCH__
    clocks_up();
    // deko3d first: if NVK then brings the program down, these results are already written.
    log("\n== deko3d\n");
    if (!run_deko3d(scenarios, all))
        log("deko3d: did not run\n");
#endif
    log("\n== vulkan\n");
    if (!run_vulkan(scenarios, all))
        log("vulkan: did not run\n");
    summary(all);
#ifdef __SWITCH__
    clocks_down();
    log("\ndone\n");
    if (g_log)
        std::fclose(g_log);
    g_log = nullptr;
    // Only now that both graphics APIs are gone: a text screen saying so.
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    std::printf("SA-nx resolve probe: done.\n\nResults are in /switch/sonicadv/probe.txt\n\n"
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
