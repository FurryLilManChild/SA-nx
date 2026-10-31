// SA-nx resolve probe (Stage 1). Times the per-pixel translucency resolve on the Switch through
// NVK (Vulkan, what the game uses) and through deko3d, on the same lists and the same shaders, so
// the slow resolves of switch-test10 to 12 can be pinned on the driver or on our shader.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace probe {

constexpr std::uint32_t kEnd = 0xFFFFFFFFu;

// One set of per-pixel lists, as the gather pass leaves them: the head of each pixel's list and
// the nodes (colour, depth, info, next).
struct Scenario {
    std::string name;
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint32_t> heads, nodes;
    std::uint32_t count() const { return static_cast<std::uint32_t>(nodes.size() / 4); }
};

struct Timing {
    std::string api, variant, scenario;
    double best_ms = 0, median_ms = 0;  // wall clock around submit and wait
    double gpu_ms = -1;                 // GPU timestamps, when the API gave them
    std::uint32_t checksum = 0;         // of the resolved picture
    bool ok = false;
    std::string note;
};

// Prints to the log (and stdout).
void log(const char* fmt, ...);

// Each runs every shader variant on every scenario. False if the API could not start.
bool run_vulkan(const std::vector<Scenario>& scenarios, std::vector<Timing>& out);
#ifdef __SWITCH__
bool run_deko3d(const std::vector<Scenario>& scenarios, std::vector<Timing>& out);
#endif

std::uint32_t checksum(const std::uint32_t* px, std::size_t n);

}  // namespace probe
