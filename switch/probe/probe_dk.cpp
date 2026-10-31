// SA-nx resolve probe: the deko3d side. The same shaders (compiled by uam instead of glslc, from
// the same GLSL), the same lists, the same full-screen draw, so the only difference from the NVK
// side is the driver and its shader compiler.
#ifdef __SWITCH__
#include <deko3d.h>
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "probe.h"
#include "probe_shaders.h"

namespace probe {
namespace {

constexpr std::uint32_t kOpaque = 0xFF604020u;  // as probe_vk.cpp

std::uint32_t align(std::uint32_t v, std::uint32_t a) {
    return (v + a - 1) & ~(a - 1);
}

void debug_cb(void*, const char* context, DkResult result, const char* message) {
    log("deko3d: %s: result %d: %s\n", context ? context : "", int(result), message ? message : "");
}

struct Push {  // std140 layout of the Push block: same offsets as the Vulkan push constants
    float scale[2];
    float offset[2];
    std::int32_t mode;
    float alpha_ref;
    std::uint32_t width, capacity, poly;
};

struct Dk {
    DkDevice dev = nullptr;
    DkMemBlock code = nullptr, data = nullptr, images = nullptr, cmdmem = nullptr;
    DkQueue queue = nullptr;
    DkCmdBuf cmd = nullptr;
    ~Dk() {
        if (queue)
            dkQueueWaitIdle(queue);
        if (cmd) dkCmdBufDestroy(cmd);
        if (queue) dkQueueDestroy(queue);
        if (cmdmem) dkMemBlockDestroy(cmdmem);
        if (images) dkMemBlockDestroy(images);
        if (data) dkMemBlockDestroy(data);
        if (code) dkMemBlockDestroy(code);
        if (dev) dkDeviceDestroy(dev);
    }
    DkMemBlock block(std::uint32_t size, std::uint32_t flags) {
        DkMemBlockMaker m;
        dkMemBlockMakerDefaults(&m, dev, align(size, DK_MEMBLOCK_ALIGNMENT));
        m.flags = flags;
        return dkMemBlockCreate(&m);
    }
    // Submits what was recorded and waits. Wall time in milliseconds, or -1 on a queue error.
    double submit() {
        const DkCmdList list = dkCmdBufFinishList(cmd);
        const auto t0 = std::chrono::steady_clock::now();
        dkQueueSubmitCommands(queue, list);
        dkQueueWaitIdle(queue);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        dkCmdBufClear(cmd);
        return dkQueueIsInErrorState(queue) ? -1.0 : ms;
    }
};

}  // namespace

bool run_deko3d(const std::vector<Scenario>& scenarios, std::vector<Timing>& out) {
    if (scenarios.empty())
        return false;
    const std::uint32_t w = scenarios[0].width, h = scenarios[0].height;
    Dk d;
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = debug_cb;
    d.dev = dkDeviceCreate(&dm);
    if (!d.dev) {
        log("deko3d: no device\n");
        return false;
    }

    // Shader code: every DKSH at its own 256-byte boundary, plus the tail the GPU may prefetch.
    std::vector<const ProbeShader*> shaders = {&kProbeVertex};
    for (const ProbeShader& v : kProbeVariants)
        shaders.push_back(&v);
    std::uint32_t code_size = DK_SHADER_CODE_UNUSABLE_SIZE;
    for (const ProbeShader* s : shaders)
        code_size += align(s->dksh_size, DK_SHADER_CODE_ALIGNMENT);
    d.code = d.block(code_size, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached |
                                    DkMemBlockFlags_Code);
    if (!d.code) {
        log("deko3d: no code memory\n");
        return false;
    }
    std::vector<DkShader> dk_shaders(shaders.size());
    {
        auto* base = static_cast<std::uint8_t*>(dkMemBlockGetCpuAddr(d.code));
        std::uint32_t at = 0;
        for (std::size_t i = 0; i < shaders.size(); ++i) {
            std::memcpy(base + at, shaders[i]->dksh, shaders[i]->dksh_size);
            DkShaderMaker sm;
            dkShaderMakerDefaults(&sm, d.code, at);
            dkShaderInitialize(&dk_shaders[i], &sm);
            if (!dkShaderIsValid(&dk_shaders[i]))
                log("deko3d: shader %s is not valid\n", shaders[i]->name);
            at += align(shaders[i]->dksh_size, DK_SHADER_CODE_ALIGNMENT);
        }
    }

    // Data: the uniform block, the counter and the descriptors, then each scenario's lists in its
    // own region (written once, so no stale cache lines between scenarios), then the opaque
    // picture's upload and the readback.
    const std::uint32_t pixels_bytes = w * h * 4;
    std::uint32_t data_size = 0x1000;
    std::vector<std::uint32_t> head_at, node_at;
    for (const Scenario& s : scenarios) {
        head_at.push_back(data_size);
        data_size += align(static_cast<std::uint32_t>(s.heads.size() * 4), 0x1000);
        node_at.push_back(data_size);
        data_size += align(std::max<std::uint32_t>(s.count() * 16, 16), 0x1000);
    }
    const std::uint32_t staging_at = data_size;
    data_size += align(pixels_bytes, 0x1000);
    const std::uint32_t readback_at = data_size;
    data_size += align(pixels_bytes, 0x1000);
    d.data = d.block(data_size, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!d.data) {
        log("deko3d: no data memory (%u bytes)\n", data_size);
        return false;
    }
    auto* cpu = static_cast<std::uint8_t*>(dkMemBlockGetCpuAddr(d.data));
    const DkGpuAddr gpu = dkMemBlockGetGpuAddr(d.data);
    const std::uint32_t ubo_at = 0, counter_at = 0x100, image_desc_at = 0x200, sampler_desc_at = 0x300;
    std::memset(cpu, 0, 0x1000);
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
        std::memcpy(cpu + head_at[i], scenarios[i].heads.data(), scenarios[i].heads.size() * 4);
        std::memcpy(cpu + node_at[i], scenarios[i].nodes.data(), scenarios[i].nodes.size() * 4);
    }
    {
        auto* px = reinterpret_cast<std::uint32_t*>(cpu + staging_at);
        for (std::uint32_t i = 0; i < w * h; ++i)
            px[i] = kOpaque;
    }

    // Images: the opaque picture (sampled) and the target (rendered into, then copied out).
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, d.dev);
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = w;
    lm.dimensions[1] = h;
    lm.flags = DkImageFlags_Usage2DEngine;
    DkImageLayout opaque_layout;
    dkImageLayoutInitialize(&opaque_layout, &lm);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    DkImageLayout target_layout;
    dkImageLayoutInitialize(&target_layout, &lm);
    const std::uint32_t target_at = align(static_cast<std::uint32_t>(dkImageLayoutGetSize(&opaque_layout)),
                                          dkImageLayoutGetAlignment(&target_layout));
    d.images = d.block(target_at + static_cast<std::uint32_t>(dkImageLayoutGetSize(&target_layout)),
                       DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!d.images) {
        log("deko3d: no image memory\n");
        return false;
    }
    DkImage opaque, target;
    dkImageInitialize(&opaque, &opaque_layout, d.images, 0);
    dkImageInitialize(&target, &target_layout, d.images, target_at);
    DkImageView opaque_view, target_view;
    dkImageViewDefaults(&opaque_view, &opaque);
    dkImageViewDefaults(&target_view, &target);
    {
        DkImageDescriptor idesc;
        dkImageDescriptorInitialize(&idesc, &opaque_view, false, false);
        std::memcpy(cpu + image_desc_at, &idesc, sizeof idesc);
        DkSampler smp;
        dkSamplerDefaults(&smp);
        smp.wrapMode[0] = smp.wrapMode[1] = smp.wrapMode[2] = DkWrapMode_ClampToEdge;
        DkSamplerDescriptor sdesc;
        dkSamplerDescriptorInitialize(&sdesc, &smp);
        std::memcpy(cpu + sampler_desc_at, &sdesc, sizeof sdesc);
    }

    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, d.dev);
    qm.flags = DkQueueFlags_Graphics;
    d.queue = dkQueueCreate(&qm);
    d.cmdmem = d.block(0x10000, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, d.dev);
    d.cmd = dkCmdBufCreate(&cm);
    if (!d.queue || !d.cmdmem || !d.cmd) {
        log("deko3d: no queue or command buffer\n");
        return false;
    }
    dkCmdBufAddMemory(d.cmd, d.cmdmem, 0, 0x10000);

    const DkImageRect rect{0, 0, 0, w, h, 1};
    {
        const DkCopyBuf src{gpu + staging_at, 0, 0};
        dkCmdBufCopyBufferToImage(d.cmd, &src, &opaque_view, &rect, 0);
        dkCmdBufBarrier(d.cmd, DkBarrier_Full,
                        DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors |
                            DkInvalidateFlags_L2Cache);
        if (d.submit() < 0) {
            log("deko3d: the opaque upload failed\n");
            return false;
        }
    }
    log("deko3d: ready (%u KB of lists and buffers)\n", data_size / 1024);

    for (std::size_t si = 0; si < scenarios.size(); ++si) {
        const Scenario& s = scenarios[si];
        if (s.width != w || s.height != h)
            continue;
        for (std::size_t vi = 0; vi < std::size(kProbeVariants); ++vi) {
            Timing t;
            t.api = "deko3d";
            t.variant = kProbeVariants[vi].name;
            t.scenario = s.name;
            std::vector<double> wall;
            for (int rep = 0; rep < 5; ++rep) {
                const bool last = rep == 4;
                dkCmdBufBindRenderTarget(d.cmd, &target_view, nullptr);
                const DkViewport vp{0, 0, float(w), float(h), 0, 1};
                const DkScissor sc{0, 0, w, h};
                dkCmdBufSetViewports(d.cmd, 0, &vp, 1);
                dkCmdBufSetScissors(d.cmd, 0, &sc, 1);
                DkRasterizerState rs;
                dkRasterizerStateDefaults(&rs);
                rs.cullMode = DkFace_None;
                dkCmdBufBindRasterizerState(d.cmd, &rs);
                DkColorState cs;
                dkColorStateDefaults(&cs);
                dkCmdBufBindColorState(d.cmd, &cs);
                DkColorWriteState cw;
                dkColorWriteStateDefaults(&cw);
                dkCmdBufBindColorWriteState(d.cmd, &cw);
                DkDepthStencilState ds;
                dkDepthStencilStateDefaults(&ds);
                ds.depthTestEnable = 0;
                ds.depthWriteEnable = 0;
                dkCmdBufBindDepthStencilState(d.cmd, &ds);
                const DkShader* bound[2] = {&dk_shaders[0], &dk_shaders[1 + vi]};
                dkCmdBufBindShaders(d.cmd, DkStageFlag_GraphicsMask, bound, 2);
                dkCmdBufBindImageDescriptorSet(d.cmd, gpu + image_desc_at, 1);
                dkCmdBufBindSamplerDescriptorSet(d.cmd, gpu + sampler_desc_at, 1);
                const DkResHandle tex[2] = {dkMakeTextureHandle(0, 0), dkMakeTextureHandle(0, 0)};
                dkCmdBufBindTextures(d.cmd, DkStage_Fragment, 0, tex, 2);
                Push push{};
                push.width = w;
                push.capacity = std::max<std::uint32_t>(s.count(), 1);
                dkCmdBufPushConstants(d.cmd, gpu + ubo_at, 0x100, 0, sizeof push, &push);
                dkCmdBufBindUniformBuffer(d.cmd, DkStage_Fragment, 0, gpu + ubo_at, 0x100);
                dkCmdBufBindStorageBuffer(d.cmd, DkStage_Fragment, 0, gpu + counter_at, 16);
                dkCmdBufBindStorageBuffer(d.cmd, DkStage_Fragment, 1, gpu + head_at[si],
                                          static_cast<std::uint32_t>(s.heads.size() * 4));
                dkCmdBufBindStorageBuffer(d.cmd, DkStage_Fragment, 2, gpu + node_at[si],
                                          std::max<std::uint32_t>(s.count() * 16, 16));
                dkCmdBufDraw(d.cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
                if (last) {
                    dkCmdBufBarrier(d.cmd, DkBarrier_Fragments, 0);
                    const DkCopyBuf dst{gpu + readback_at, 0, 0};
                    dkCmdBufCopyImageToBuffer(d.cmd, &target_view, &rect, &dst, 0);
                    dkCmdBufBarrier(d.cmd, DkBarrier_Full, 0);
                }
                const double ms = d.submit();
                if (ms < 0) {
                    t.note = "the queue failed";
                    break;
                }
                wall.push_back(ms);
                if (last)
                    t.checksum = checksum(reinterpret_cast<const std::uint32_t*>(cpu + readback_at),
                                          std::size_t{w} * h);
                if (ms > 1500.0)
                    break;
            }
            if (!wall.empty()) {
                std::sort(wall.begin(), wall.end());
                t.best_ms = wall.front();
                t.median_ms = wall[wall.size() / 2];
                t.ok = t.note.empty();
            }
            log("  deko3d %-12s %-14s wall %8.2f ms (best %8.2f)  sum %08x %s\n", t.variant.c_str(),
                t.scenario.c_str(), t.median_ms, t.best_ms, t.checksum, t.note.c_str());
            out.push_back(t);
            if (!t.note.empty())
                return true;  // a failed queue stays failed
        }
    }
    return true;
}

}  // namespace probe
#endif
