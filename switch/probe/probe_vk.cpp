// SA-nx resolve probe: the Vulkan side (NVK on the Switch, lavapipe or any driver on Linux). The
// resolve runs as the game runs it: a full-screen triangle, the per-pixel buffers in set 1, the
// opaque colour sampled through texelFetch, the width and capacity in push constants.
#include <algorithm>
#include <chrono>
#include <cstring>

#include "dream/render/vk/context.h"
#include "dream/render/vk/resources.h"
#include "probe.h"
#include "probe_shaders.h"

namespace probe {
namespace {

using dream::render::vk::AttachmentImage;
using dream::render::vk::Context;
using dream::render::vk::DeviceBuffer;
using dream::render::vk::HostBuffer;

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kOpaque = 0xFF604020u;  // RGBA8 (little-endian ABGR): a dark blue-grey

struct Push {  // the shaders' Push block: scale, offset, mode, alpha_ref, width, capacity, poly
    float scale[2];
    float offset[2];
    std::int32_t mode;
    float alpha_ref;
    std::uint32_t width, capacity, poly;
};

VkShaderModule module(VkDevice dev, const ProbeShader& s) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = s.spv_size;
    ci.pCode = reinterpret_cast<const std::uint32_t*>(s.spv);
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &ci, nullptr, &m);
    return m;
}

struct Vk {
    Context ctx;
    VkDevice dev = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    double tick_ns = 1.0;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkDescriptorSetLayout empty_layout = VK_NULL_HANDLE, oit_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    AttachmentImage target, opaque;
    DeviceBuffer counter, heads, nodes;
    HostBuffer staging, readback;
    std::uint32_t width = 0, height = 0;

    ~Vk() {
        if (!dev)
            return;
        vkDeviceWaitIdle(dev);
        if (sampler) vkDestroySampler(dev, sampler, nullptr);
        if (dpool) vkDestroyDescriptorPool(dev, dpool, nullptr);
        if (layout) vkDestroyPipelineLayout(dev, layout, nullptr);
        if (oit_layout) vkDestroyDescriptorSetLayout(dev, oit_layout, nullptr);
        if (empty_layout) vkDestroyDescriptorSetLayout(dev, empty_layout, nullptr);
        if (fb) vkDestroyFramebuffer(dev, fb, nullptr);
        if (pass) vkDestroyRenderPass(dev, pass, nullptr);
        if (queries) vkDestroyQueryPool(dev, queries, nullptr);
        if (fence) vkDestroyFence(dev, fence, nullptr);
        if (pool) vkDestroyCommandPool(dev, pool, nullptr);
        target.destroy();
        opaque.destroy();
        counter.destroy();
        heads.destroy();
        nodes.destroy();
        staging.destroy();
        readback.destroy();
    }

    bool begin() {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        return vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS;
    }
    // Submits and waits (5 s at most). Returns the wall time in milliseconds, or -1.
    double submit() {
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            return -1;
        vkResetFences(dev, 1, &fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        const auto t0 = std::chrono::steady_clock::now();
        if (vkQueueSubmit(ctx.queue(), 1, &si, fence) != VK_SUCCESS)
            return -1;
        if (vkWaitForFences(dev, 1, &fence, VK_TRUE, 5'000'000'000ull) != VK_SUCCESS)
            return -1;
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
            .count();
    }
};

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags sa,
             VkPipelineStageFlags dst, VkAccessFlags da) {
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = sa;
    mb.dstAccessMask = da;
    vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                   VkPipelineStageFlags src, VkAccessFlags sa, VkPipelineStageFlags dst,
                   VkAccessFlags da) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = sa;
    b.dstAccessMask = da;
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool setup(Vk& v, std::uint32_t w, std::uint32_t h, std::size_t max_nodes) {
    if (!v.ctx.create({}, false)) {
        log("vulkan: %s\n", v.ctx.error().c_str());
        return false;
    }
    v.dev = v.ctx.device();
    v.width = w;
    v.height = h;
    log("vulkan: %s\n", v.ctx.caps().device_name.c_str());
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(v.ctx.physical_device(), &props);
    v.tick_ns = props.limits.timestampPeriod;

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = v.ctx.queue_family();
    vkCreateCommandPool(v.dev, &cpci, nullptr, &v.pool);
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = v.pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(v.dev, &cbai, &v.cmd);
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(v.dev, &fci, nullptr, &v.fence);
    VkQueryPoolCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qci.queryCount = 2;
    if (vkCreateQueryPool(v.dev, &qci, nullptr, &v.queries) != VK_SUCCESS)
        v.queries = VK_NULL_HANDLE;

    const VkExtent2D ext{w, h};
    if (!v.target.create(v.ctx, kFormat, ext,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT) ||
        !v.opaque.create(v.ctx, kFormat, ext,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT)) {
        log("vulkan: cannot create the images\n");
        return false;
    }
    const VkBufferUsageFlags su = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const VkDeviceSize head_bytes = VkDeviceSize{w} * h * 4, node_bytes = max_nodes * 16 + 16;
    if (!v.counter.create(v.ctx, 16, su) || !v.heads.create(v.ctx, head_bytes, su) ||
        !v.nodes.create(v.ctx, node_bytes, su) ||
        !v.staging.ensure(v.ctx, std::max(head_bytes, node_bytes), VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !v.readback.ensure(v.ctx, head_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
        log("vulkan: cannot create the buffers\n");
        return false;
    }

    // The opaque picture the translucency goes over: one colour.
    {
        std::vector<std::uint32_t> px(std::size_t{w} * h, kOpaque);
        v.staging.write(px.data(), px.size() * 4);
        v.begin();
        image_barrier(v.cmd, v.opaque.image(), VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy bic{};
        bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        bic.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(v.cmd, v.staging.handle(), v.opaque.image(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
        image_barrier(v.cmd, v.opaque.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
        if (v.submit() < 0) {
            log("vulkan: the opaque upload failed\n");
            return false;
        }
    }

    VkAttachmentDescription ad{};
    ad.format = kFormat;
    ad.samples = VK_SAMPLE_COUNT_1_BIT;
    ad.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ad.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sd{};
    sd.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sd.colorAttachmentCount = 1;
    sd.pColorAttachments = &ar;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &ad;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sd;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    vkCreateRenderPass(v.dev, &rpci, nullptr, &v.pass);
    const VkImageView view = v.target.view();
    VkFramebufferCreateInfo fbci{};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = v.pass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &view;
    fbci.width = w;
    fbci.height = h;
    fbci.layers = 1;
    vkCreateFramebuffer(v.dev, &fbci, nullptr, &v.fb);

    // The game's layout: set 0 (textures, unused by the resolve), set 1 the per-pixel buffers and
    // the opaque depth and colour, push constants for both stages.
    VkDescriptorSetLayoutCreateInfo eci{};
    eci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    vkCreateDescriptorSetLayout(v.dev, &eci, nullptr, &v.empty_layout);
    VkDescriptorSetLayoutBinding b[5]{};
    for (std::uint32_t i = 0; i < 5; ++i) {
        b[i].binding = i;
        b[i].descriptorType =
            i < 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo oci{};
    oci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    oci.bindingCount = 5;
    oci.pBindings = b;
    vkCreateDescriptorSetLayout(v.dev, &oci, nullptr, &v.oit_layout);
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                              sizeof(Push)};
    const VkDescriptorSetLayout layouts[2] = {v.empty_layout, v.oit_layout};
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 2;
    plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &range;
    vkCreatePipelineLayout(v.dev, &plci, nullptr, &v.layout);

    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = sizes;
    vkCreateDescriptorPool(v.dev, &dpci, nullptr, &v.dpool);
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = v.dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &v.oit_layout;
    vkAllocateDescriptorSets(v.dev, &dsai, &v.set);
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(v.dev, &sci, nullptr, &v.sampler);
    const VkDescriptorBufferInfo bi[3] = {{v.counter.handle(), 0, VK_WHOLE_SIZE},
                                          {v.heads.handle(), 0, VK_WHOLE_SIZE},
                                          {v.nodes.handle(), 0, VK_WHOLE_SIZE}};
    const VkDescriptorImageInfo ii{v.sampler, v.opaque.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr[5]{};
    for (std::uint32_t i = 0; i < 5; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = v.set;
        wr[i].dstBinding = i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = b[i].descriptorType;
        if (i < 3)
            wr[i].pBufferInfo = &bi[i];
        else
            wr[i].pImageInfo = &ii;  // the depth slot gets the colour image: the resolve never reads it
    }
    vkUpdateDescriptorSets(v.dev, 5, wr, 0, nullptr);
    return true;
}

VkPipeline pipeline(Vk& v, VkShaderModule vs, VkShaderModule fs) {
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vs;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = fs;
    st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const VkViewport vp{0, 0, float(v.width), float(v.height), 0, 1};
    const VkRect2D sc{{0, 0}, {v.width, v.height}};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &sc;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = st;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vps;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.layout = v.layout;
    gp.renderPass = v.pass;
    VkPipeline p = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(v.dev, VK_NULL_HANDLE, 1, &gp, nullptr, &p);
    return p;
}

bool upload(Vk& v, const Scenario& s) {
    const VkDeviceSize hb = s.heads.size() * 4, nb = s.nodes.size() * 4;
    v.staging.write(s.heads.data(), hb);
    v.begin();
    VkBufferCopy c{0, 0, hb};
    vkCmdCopyBuffer(v.cmd, v.staging.handle(), v.heads.handle(), 1, &c);
    if (v.submit() < 0)
        return false;
    if (nb) {
        v.staging.write(s.nodes.data(), nb);
        v.begin();
        c.size = nb;
        vkCmdCopyBuffer(v.cmd, v.staging.handle(), v.nodes.handle(), 1, &c);
        if (v.submit() < 0)
            return false;
    }
    return true;
}

}  // namespace

bool run_vulkan(const std::vector<Scenario>& scenarios, std::vector<Timing>& out) {
    if (scenarios.empty())
        return false;
    std::size_t max_nodes = 0;
    for (const Scenario& s : scenarios)
        max_nodes = std::max<std::size_t>(max_nodes, s.count());
    Vk v;
    if (!setup(v, scenarios[0].width, scenarios[0].height, max_nodes))
        return false;
    const VkShaderModule vs = module(v.dev, kProbeVertex);
    for (const Scenario& s : scenarios) {
        if (s.width != v.width || s.height != v.height || !upload(v, s)) {
            log("vulkan: cannot load %s\n", s.name.c_str());
            continue;
        }
        for (const ProbeShader& variant : kProbeVariants) {
            Timing t;
            t.api = "nvk";
            t.variant = variant.name;
            t.scenario = s.name;
            const VkShaderModule fs = module(v.dev, variant);
            const VkPipeline pl = fs ? pipeline(v, vs, fs) : VK_NULL_HANDLE;
            if (!pl) {
                t.note = "no pipeline";
                out.push_back(t);
                if (fs)
                    vkDestroyShaderModule(v.dev, fs, nullptr);
                continue;
            }
            std::vector<double> wall, gpu;
            for (int rep = 0; rep < 5; ++rep) {
                v.begin();
                vkCmdFillBuffer(v.cmd, v.counter.handle(), 0, VK_WHOLE_SIZE, 0);
                barrier(v.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                if (v.queries) {
                    vkCmdResetQueryPool(v.cmd, v.queries, 0, 2);
                    vkCmdWriteTimestamp(v.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, v.queries, 0);
                }
                VkRenderPassBeginInfo rpbi{};
                rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rpbi.renderPass = v.pass;
                rpbi.framebuffer = v.fb;
                rpbi.renderArea.extent = {v.width, v.height};
                vkCmdBeginRenderPass(v.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(v.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl);
                vkCmdBindDescriptorSets(v.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, v.layout, 1, 1,
                                        &v.set, 0, nullptr);
                Push push{};
                push.width = v.width;
                push.capacity = std::max<std::uint32_t>(s.count(), 1);
                vkCmdPushConstants(v.cmd, v.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof push, &push);
                vkCmdDraw(v.cmd, 3, 1, 0, 0);
                vkCmdEndRenderPass(v.cmd);
                if (v.queries)
                    vkCmdWriteTimestamp(v.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, v.queries, 1);
                const bool last = rep == 4;
                if (last) {
                    VkBufferImageCopy bic{};
                    bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    bic.imageExtent = {v.width, v.height, 1};
                    vkCmdCopyImageToBuffer(v.cmd, v.target.image(),
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           v.readback.handle(), 1, &bic);
                    barrier(v.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
                }
                const double ms = v.submit();
                if (ms < 0) {
                    t.note = "the GPU did not finish within 5 s";
                    break;
                }
                wall.push_back(ms);
                std::uint64_t ts[2]{};
                if (v.queries &&
                    vkGetQueryPoolResults(v.dev, v.queries, 0, 2, sizeof ts, ts, 8,
                                          VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
                    gpu.push_back(static_cast<double>(ts[1] - ts[0]) * v.tick_ns / 1e6);
                if (last) {
                    v.readback.invalidate();
                    t.checksum = checksum(static_cast<const std::uint32_t*>(v.readback.mapped()),
                                          std::size_t{v.width} * v.height);
                }
                if (ms > 1500.0)
                    break;  // a slow variant: enough samples
            }
            if (!wall.empty()) {
                std::sort(wall.begin(), wall.end());
                t.best_ms = wall.front();
                t.median_ms = wall[wall.size() / 2];
                if (!gpu.empty()) {
                    std::sort(gpu.begin(), gpu.end());
                    t.gpu_ms = gpu[gpu.size() / 2];
                }
                t.ok = t.note.empty();
            }
            log("  nvk %-12s %-14s wall %8.2f ms (best %8.2f)  gpu %8.2f ms  sum %08x %s\n",
                t.variant.c_str(), t.scenario.c_str(), t.median_ms, t.best_ms, t.gpu_ms,
                t.checksum, t.note.c_str());
            out.push_back(t);
            vkDestroyPipeline(v.dev, pl, nullptr);
            vkDestroyShaderModule(v.dev, fs, nullptr);
            if (!t.note.empty() && t.note[0] == 't')
                return true;  // the device may be lost: stop here, keep what was measured
        }
    }
    vkDestroyShaderModule(v.dev, vs, nullptr);
    return true;
}

}  // namespace probe
