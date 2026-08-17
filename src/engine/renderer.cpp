// renderer.cpp
#include "renderer.h"
#include <cstring>
#include <array>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// renderer.cpp
#include "renderer.h"
#include <cstring>
#include <array>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

void VulkanContext::init(uint32_t w, uint32_t h) {
    fprintf(stderr, "[dbg] vk init (windowed)\n");

    // ---- GLFW window ----
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow((int)w, (int)h, "Tower of the Sorcerer", nullptr, nullptr);

    // ---- Vulkan instance (with surface extensions) ----
    VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.apiVersion = VK_API_VERSION_1_0;
    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    VkInstanceCreateInfo ici{}; ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = extCount; ici.ppEnabledExtensionNames = glfwExts;
    vk_check(vkCreateInstance(&ici, nullptr, &instance), "instance");

    // ---- Surface ----
    vk_check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "surface");

    // ---- Physical device ----
    uint32_t n = 0; vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> phys(n); vkEnumeratePhysicalDevices(instance, &n, phys.data());
    physical = phys[0];

    // ---- Queue family (graphics + present on same queue) ----
    uint32_t qc = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qc); vkGetPhysicalDeviceQueueFamilyProperties(physical, &qc, qf.data());
    for (uint32_t i = 0; i < qc; i++) {
        VkBool32 presentOk = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &presentOk);
        if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentOk) { gfxFamily = i; break; }
    }

    // ---- Logical device (with swapchain extension) ----
    float pr = 1.0f;
    VkDeviceQueueCreateInfo qci{}; qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = gfxFamily; qci.queueCount = 1; qci.pQueuePriorities = &pr;
    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{}; dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExts;
    vk_check(vkCreateDevice(physical, &dci, nullptr, &device), "device");
    vkGetDeviceQueue(device, gfxFamily, 0, &gfxQueue);

    // ---- Choose swap surface format ----
    uint32_t fmtCount = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, fmts.data());
    swapFormat = fmts[0].format;
    VkColorSpaceKHR colorSpace = fmts[0].colorSpace;
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapFormat = f.format; colorSpace = f.colorSpace; break;
        }
    }

    // ---- Swapchain ----
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    swapImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && swapImageCount > caps.maxImageCount)
        swapImageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{}; sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface; sci.minImageCount = swapImageCount;
    sci.imageFormat = swapFormat; sci.imageColorSpace = colorSpace;
    sci.imageExtent = {w, h}; sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    vk_check(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain), "swapchain");

    // ---- Swapchain images + views ----
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, nullptr);
    swapImages.resize(swapImageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages.data());
    swapViews.resize(swapImageCount);
    for (uint32_t i = 0; i < swapImageCount; i++) {
        VkImageViewCreateInfo iv{}; iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = swapImages[i]; iv.viewType = VK_IMAGE_VIEW_TYPE_2D; iv.format = swapFormat;
        iv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk_check(vkCreateImageView(device, &iv, nullptr, &swapViews[i]), "swap_view");
    }

    // ---- Sync objects ----
    VkSemaphoreCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk_check(vkCreateSemaphore(device, &si, nullptr, &imageAvailable[i]), "sem_img");
        vk_check(vkCreateSemaphore(device, &si, nullptr, &renderFinished[i]), "sem_rnd");
        vk_check(vkCreateFence(device, &fi, nullptr, &inFlight[i]), "fence");
    }
}

bool VulkanContext::acquireNext() {
    vkWaitForFences(device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                       imageAvailable[currentFrame], VK_NULL_HANDLE,
                                       &currentImageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;
    vkResetFences(device, 1, &inFlight[currentFrame]);
    return true;
}

void VulkanContext::present() {
    VkPresentInfoKHR pi{}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderFinished[currentFrame];
    pi.swapchainCount = 1; pi.pSwapchains = &swapchain;
    pi.pImageIndices = &currentImageIndex;
    vkQueuePresentKHR(gfxQueue, &pi);
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanContext::destroy() {
    if (device) {
        vkDeviceWaitIdle(device);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (imageAvailable[i]) vkDestroySemaphore(device, imageAvailable[i], nullptr);
            if (renderFinished[i]) vkDestroySemaphore(device, renderFinished[i], nullptr);
            if (inFlight[i])       vkDestroyFence(device, inFlight[i], nullptr);
        }
        for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (surface)  vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    if (window)   { glfwDestroyWindow(window); glfwTerminate(); }
}



// ---- Renderer ----
static VkShaderModule g_vs, g_fs;

void Renderer::init(uint32_t w, uint32_t h) {
    W = w; H = h;
    vk.init(w, h);
    // command pool
    VkCommandPoolCreateInfo cp{}; cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp.queueFamilyIndex = vk.gfxFamily; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vk_check(vkCreateCommandPool(vk.device, &cp, nullptr, &cmdPool), "cmdpool");

    // render pass (present to swapchain)
    VkAttachmentDescription att{}; att.format = vk.swapFormat;
    att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{}; ref.attachment = 0; ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
    VkSubpassDependency dep{}; dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1; rpi.pAttachments = &att; rpi.subpassCount = 1; rpi.pSubpasses = &sp;
    rpi.dependencyCount = 1; rpi.pDependencies = &dep;
    vk_check(vkCreateRenderPass(vk.device, &rpi, nullptr, &renderPass), "renderpass");

    // per-swapchain-image framebuffers + command buffers
    swapFBs.resize(vk.swapImageCount);
    for (uint32_t i = 0; i < vk.swapImageCount; i++) {
        VkFramebufferCreateInfo fbi{}; fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = renderPass; fbi.attachmentCount = 1; fbi.pAttachments = &vk.swapViews[i];
        fbi.width = W; fbi.height = H; fbi.layers = 1;
        vk_check(vkCreateFramebuffer(vk.device, &fbi, nullptr, &swapFBs[i]), "framebuffer");
    }
    cmdBufs.resize(vk.swapImageCount);
    VkCommandBufferAllocateInfo cba{}; cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = cmdPool; cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = vk.swapImageCount;
    vk_check(vkAllocateCommandBuffers(vk.device, &cba, cmdBufs.data()), "cmdbuf");

    // shaders (look in ./shaders/ relative to CWD; CMake copies them next to the exe)
    g_vs = loadSpv(vk.device, "sprite.vert.spv");
    g_fs = loadSpv(vk.device, "sprite.frag.spv");

    // descriptor layout: binding 0 = combined sampler2D
    VkDescriptorSetLayoutBinding b{}; b.binding = 0; b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{}; dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 1; dsl.pBindings = &b;
    vk_check(vkCreateDescriptorSetLayout(vk.device, &dsl, nullptr, &dsLayout), "dslayout");

    // pipeline layout with push constant (res)
    VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; pc.offset = 0; pc.size = 32;  // vec2 res + pad + vec4 xform (std140)
    VkPipelineLayoutCreateInfo pl{}; pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1; pl.pSetLayouts = &dsLayout; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pc;
    vk_check(vkCreatePipelineLayout(vk.device, &pl, nullptr, &pipeLayout), "pipelayout");

    // pipeline (vertex: pos2, rect4, uvRC4, tint4, solid1 = 15 floats)
    VkVertexInputBindingDescription bind{};
    bind.binding = 0; bind.stride = 15 * 4; bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 5> attr{};
    attr[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};        // aPos
    attr[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8};  // aRect
    attr[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24}; // aUVrc
    attr[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 40}; // aTint
    attr[4] = {4, 0, VK_FORMAT_R32_SFLOAT, 56};          // aSolid (flat-color flag)
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 5; vi.pVertexAttributeDescriptions = attr.data();
    VkPipelineShaderStageCreateInfo ss[2]{};
    ss[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ss[0].stage = VK_SHADER_STAGE_VERTEX_BIT; ss[0].module = g_vs; ss[0].pName = "main";
    ss[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ss[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; ss[1].module = g_fs; ss[1].pName = "main";
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    VkViewport vpv{}; vpv.x=0; vpv.y=0; vpv.width=(float)W; vpv.height=(float)H; vpv.minDepth=0; vpv.maxDepth=1;
    VkRect2D scr{}; scr.offset={0,0}; scr.extent={W,H};
    vp.viewportCount = 1; vp.pViewports = &vpv; vp.scissorCount = 1; vp.pScissors = &scr;
    // dynamic viewport + scissor so the 4-way split diagnostic can re-scissor per pass
    VkDynamicState dynS[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyns{}; dyns.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dyns.dynamicStateCount=2; dyns.pDynamicStates=dynS;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cb{}; cb.blendEnable = VK_TRUE;
    cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo bl{}; bl.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    bl.attachmentCount = 1; bl.pAttachments = &cb;
    VkGraphicsPipelineCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2; pi.pStages = ss; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pColorBlendState = &bl;
    pi.pDynamicState = &dyns;   // dynamic viewport+scissor (for split diagnostic)
    pi.layout = pipeLayout; pi.renderPass = renderPass; pi.subpass = 0;
    vk_check(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline), "pipeline");

    // sampler
    VkSamplerCreateInfo sa{}; sa.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sa.magFilter = VK_FILTER_NEAREST; sa.minFilter = VK_FILTER_NEAREST;
    sa.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sa.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sa.maxLod = 1.0f;
    vk_check(vkCreateSampler(vk.device, &sa, nullptr, &sampler), "sampler");

    // descriptor pool (sprite atlas + font atlas + room for runtime Textures)
    VkDescriptorPoolSize ps{}; ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 32;
    VkDescriptorPoolCreateInfo dpi{}; dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 32; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
    vk_check(vkCreateDescriptorPool(vk.device, &dpi, nullptr, &dsPool), "dspool");
    spriteSet = VK_NULL_HANDLE; fontSet = VK_NULL_HANDLE;

    // dummy 1x1 solid-color texture (transparent black). Solid quads sample this
    // so the shader can output flat tint without pulling the sprite atlas.
    {
        std::vector<uint8_t> px(4, 0);   // RGBA = 0,0,0,0
        uploadAtlas(px, 1, 1, solidImg_, solidView_, solidSet_, solidMem_);
    }
    fprintf(stderr, "[dbg] renderer init done\n");
}

// Free every Vulkan object this Renderer owns, with NULL_HANDLE guards so a
// zero-handle never reaches vkDestroy* (which would fault with "Invalid device").
// Called before the device is torn down so we don't leak GPU memory at exit.
void Renderer::destroy() {
    if (vk.device == VK_NULL_HANDLE) return;
    auto dv = [&](VkDevice d, VkImage& i){ if (i!=VK_NULL_HANDLE){ vkDestroyImage(d,i,nullptr); i=VK_NULL_HANDLE; } };
    auto dvw= [&](VkDevice d, VkImageView& v){ if (v!=VK_NULL_HANDLE){ vkDestroyImageView(d,v,nullptr); v=VK_NULL_HANDLE; } };
    auto df = [&](VkDevice d, VkDeviceMemory& m){ if (m!=VK_NULL_HANDLE){ vkFreeMemory(d,m,nullptr); m=VK_NULL_HANDLE; } };
    auto db = [&](VkDevice d, VkBuffer& b){ if (b!=VK_NULL_HANDLE){ vkDestroyBuffer(d,b,nullptr); b=VK_NULL_HANDLE; } };
    dv(vk.device, spriteAtlas_.img); dvw(vk.device, spriteAtlas_.view); df(vk.device, spriteAtlas_.mem);
    dv(vk.device, fontAtlas_.img);   dvw(vk.device, fontAtlas_.view);   df(vk.device, fontAtlas_.mem);
    dv(vk.device, solidImg_);        dvw(vk.device, solidView_);        df(vk.device, solidMem_);
    db(vk.device, vbuf); df(vk.device, vbufMem); vbufCap=0;
    db(vk.device, ibuf); df(vk.device, ibufMem); ibufCap=0;
    for (auto& swfb : swapFBs) if (swfb) { vkDestroyFramebuffer(vk.device, swfb, nullptr); swfb=VK_NULL_HANDLE; }
    swapFBs.clear();
    if (renderPass!= VK_NULL_HANDLE){ vkDestroyRenderPass(vk.device, renderPass, nullptr); renderPass=VK_NULL_HANDLE; }
    if (pipeline  != VK_NULL_HANDLE){ vkDestroyPipeline(vk.device, pipeline, nullptr); pipeline=VK_NULL_HANDLE; }
    if (pipeLayout!= VK_NULL_HANDLE){ vkDestroyPipelineLayout(vk.device, pipeLayout, nullptr); pipeLayout=VK_NULL_HANDLE; }
    if (dsLayout != VK_NULL_HANDLE){ vkDestroyDescriptorSetLayout(vk.device, dsLayout, nullptr); dsLayout=VK_NULL_HANDLE; }
    if (sampler   != VK_NULL_HANDLE){ vkDestroySampler(vk.device, sampler, nullptr); sampler=VK_NULL_HANDLE; }
    if (dsPool    != VK_NULL_HANDLE){ vkDestroyDescriptorPool(vk.device, dsPool, nullptr); dsPool=VK_NULL_HANDLE; }
    if (cmdPool   != VK_NULL_HANDLE){ vkDestroyCommandPool(vk.device, cmdPool, nullptr); cmdPool=VK_NULL_HANDLE; }
    vk.destroy();   // drops instance/device/queue (also NULL_HANDLE-guarded)
}

void Renderer::loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) {
    const uint32_t COLS = 9;
    uint32_t AW=0, AH=0; std::vector<uint8_t> atlas;
    if (!packAtlas(layers, sw, sh, COLS, atlas, AW, AH)) {
        fprintf(stderr, "[err] loadSprites: bad layers\n"); return;
    }
    uploadAtlas(atlas, AW, AH, spriteAtlas_.img, spriteAtlas_.view, spriteAtlas_.set, spriteAtlas_.mem);
    spriteSet = spriteAtlas_.set; spriteAtlas_.w=AW; spriteAtlas_.h=AH;
}

void Renderer::loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    uploadAtlas(px, w, h, fontAtlas_.img, fontAtlas_.view, fontAtlas_.set, fontAtlas_.mem);
    fontSet = fontAtlas_.set; fontAtlas_.w=w; fontAtlas_.h=h;
}

// Re-upload the font atlas after it grew / gained glyphs at runtime. Frees the
// previous font image/view/mem first so repeated updates don't leak GPU memory.
void Renderer::updateFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    if (fontAtlas_.img != VK_NULL_HANDLE) {
        if (fontAtlas_.view != VK_NULL_HANDLE) vkDestroyImageView(vk.device, fontAtlas_.view, nullptr);
        if (fontAtlas_.mem  != VK_NULL_HANDLE) vkFreeMemory(vk.device, fontAtlas_.mem, nullptr);
        vkDestroyImage(vk.device, fontAtlas_.img, nullptr);
        // descriptor set is pooled; not freed individually
        fontAtlas_.img = VK_NULL_HANDLE; fontAtlas_.view = VK_NULL_HANDLE;
        fontAtlas_.mem = VK_NULL_HANDLE; fontAtlas_.set = VK_NULL_HANDLE;
    }
    uploadAtlas(px, w, h, fontAtlas_.img, fontAtlas_.view, fontAtlas_.set, fontAtlas_.mem);
    // uploadAtlas allocates a fresh descriptor set; surface it to drawText
    fontSet = fontAtlas_.set; fontAtlas_.w = w; fontAtlas_.h = h;
}

// Upload RGBA pixels directly into a LINEAR-tiled 2D image (avoids lavapipe copy bug).
void Renderer::uploadAtlas(const std::vector<uint8_t>& px, uint32_t w, uint32_t h, VkImage& img, VkImageView& view, VkDescriptorSet& set, VkDeviceMemory& mem) {
    mem = VK_NULL_HANDLE;
    createImage(vk.device, vk.physical, w, h, VK_FORMAT_R8G8B8A8_SRGB,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, img, view, 1, &mem, 0, VK_IMAGE_TILING_LINEAR, true);
    // transition to GENERAL for direct CPU write (linear images can be mapped)
    VkCommandBuffer cb = beginOnce(vk.device, cmdPool);
    VkImageMemoryBarrier bar{}; bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; bar.newLayout=VK_IMAGE_LAYOUT_GENERAL; bar.image=img;
    bar.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    bar.srcAccessMask=0; bar.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,0,nullptr,0,nullptr,1,&bar);
    endSubmit(vk.device, vk.gfxQueue, cmdPool, cb);
    // map and write pixels with row pitch
    VkImageSubresource sr{}; sr.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout lay{}; vkGetImageSubresourceLayout(vk.device, img, &sr, &lay);
    void* p=nullptr; vkMapMemory(vk.device, mem, 0, lay.size, 0, &p);
    const uint8_t* src = px.data();
    for (uint32_t y=0; y<h; y++) {
        memcpy((uint8_t*)p + lay.offset + y*lay.rowPitch, src + y*w*4, w*4);
    }
    vkUnmapMemory(vk.device, mem);
    // transition to SHADER_READ_ONLY_OPTIMAL for sampling
    cb = beginOnce(vk.device, cmdPool);
    bar.oldLayout=VK_IMAGE_LAYOUT_GENERAL; bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT; bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,0,nullptr,0,nullptr,1,&bar);
    endSubmit(vk.device, vk.gfxQueue, cmdPool, cb);
    // descriptor set
    VkDescriptorSet s; VkDescriptorSetAllocateInfo ai2{}; ai2.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai2.descriptorPool=dsPool; ai2.descriptorSetCount=1; ai2.pSetLayouts=&dsLayout;
    vk_check(vkAllocateDescriptorSets(vk.device,&ai2,&s),"ads");
    VkDescriptorImageInfo di{}; di.imageView=view; di.sampler=sampler; di.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{}; wd.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wd.dstSet=s; wd.dstBinding=0; wd.descriptorCount=1; wd.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wd.pImageInfo=&di;
    vkUpdateDescriptorSets(vk.device,1,&wd,0,nullptr);
    set = s;
}

void Renderer::transitionImage(VkImage img, VkImageLayout from, VkImageLayout to, VkImageAspectFlags asp) {
    VkCommandBuffer cb = beginOnce(vk.device, cmdPool);
    VkImageMemoryBarrier bar{}; bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout=from; bar.newLayout=to; bar.image=img; bar.subresourceRange={asp,0,1,0,1};
    bar.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,0,nullptr,0,nullptr,1,&bar);
    endSubmit(vk.device, vk.gfxQueue, cmdPool, cb);
}

void Renderer::ensureVertexBuffer(size_t needBytes) {
    if (vbufCap >= needBytes) return;
    if (vbuf) { vkDestroyBuffer(vk.device, vbuf, nullptr); vkFreeMemory(vk.device, vbufMem, nullptr); }
    vbufCap = needBytes * 2 + 65536;
    VkBufferCreateInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bi.size=vbufCap; bi.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vk_check(vkCreateBuffer(vk.device,&bi,nullptr,&vbuf),"vb");
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(vk.device,vbuf,&mr);
    VkPhysicalDeviceMemoryProperties pdmp; vkGetPhysicalDeviceMemoryProperties(vk.physical,&pdmp);
    uint32_t mi=0; for(uint32_t i=0;i<pdmp.memoryTypeCount;i++) if(mr.memoryTypeBits&(1u<<i)&&(pdmp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)&&(pdmp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){mi=i;break;}
    VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize=mr.size; ai.memoryTypeIndex=mi;
    vk_check(vkAllocateMemory(vk.device,&ai,nullptr,&vbufMem),"vbm"); vkBindBufferMemory(vk.device,vbuf,vbufMem,0);
}

void Renderer::ensureIndexBuffer(size_t needBytes) {
    if (ibufCap >= needBytes) return;
    if (ibuf) { vkDestroyBuffer(vk.device, ibuf, nullptr); vkFreeMemory(vk.device, ibufMem, nullptr); }
    ibufCap = needBytes * 2 + 65536;
    VkBufferCreateInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bi.size=ibufCap; bi.usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    vk_check(vkCreateBuffer(vk.device,&bi,nullptr,&ibuf),"ib");
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(vk.device,ibuf,&mr);
    VkPhysicalDeviceMemoryProperties pdmp; vkGetPhysicalDeviceMemoryProperties(vk.physical,&pdmp);
    uint32_t mi=0; for(uint32_t i=0;i<pdmp.memoryTypeCount;i++) if(mr.memoryTypeBits&(1u<<i)&&(pdmp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)&&(pdmp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){mi=i;break;}
    VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize=mr.size; ai.memoryTypeIndex=mi;
    vk_check(vkAllocateMemory(vk.device,&ai,nullptr,&ibufMem),"ibm"); vkBindBufferMemory(vk.device,ibuf,ibufMem,0);
}

void Renderer::begin() { sprites.clear(); texts.clear(); }
static int g_dbgQuads = -1;   // <0 = off; set by first call from TOMS_RENDER_DEBUG env
static uint8_t g_node = 0;    // current "node" tag for setNode() diagnostic
static uint8_t g_filter = 0;  // if !=0, only emit quads whose node == g_filter (split diagnostic)
void Renderer::setNode(uint8_t n) { g_node = n; }
void Renderer::setNodeFilter(uint8_t n) { g_filter = n; }
void Renderer::drawSprite(const Quad& q) {
    if (g_filter && g_node != g_filter) return;   // split diagnostic: isolate one node (by current tag)
    if (g_dbgQuads < 0) {
        const char* e = std::getenv("TOMS_RENDER_DEBUG");
        g_dbgQuads = (e && e[0]=='1') ? 1 : 0;
    }
    if (g_dbgQuads) {
        std::fprintf(stderr, "[quad] sprite rect=%.0f,%.0f %.0fx%.0f uv=[%.3f,%.3f,%.3f,%.3f] solid=%d tint=%.2f,%.2f,%.2f,%.2f\n",
            q.rect[0], q.rect[1], q.rect[2], q.rect[3],
            q.uv[0], q.uv[1], q.uv[2], q.uv[3], q.solid?1:0,
            q.tint[0], q.tint[1], q.tint[2], q.tint[3]);
    }
    Quad q2 = q; q2.node = g_node;
    sprites.push_back(q2);
}
void Renderer::drawText(const Quad& q)   { if (g_filter && g_node != g_filter) return; Quad q2 = q; q2.node = g_node; texts.push_back(q2); }

#include "batch_renderer.h"

void Renderer::end() {
    // ---- batch quads by texture-set / blend (FM79979 group-then-flush design) ----
    BatchRenderer br;
    br.begin();
    for (auto& q : sprites) br.add(q, (void*)spriteSet, 0);   // sprite atlas, default blend
    for (auto& q : texts)   br.add(q, (void*)fontSet,   0);   // font atlas,   default blend
    br.flush();
    lastDrawCalls = br.drawCalls;
    lastQuadCount = br.quadCount;

    size_t verts = br.vbuf.size();
    size_t idxs  = br.ibuf.size();
    if (verts == 0) return;

    // Parallel per-quad node tags (quad order == sprites then texts == br.vbuf order).
    std::vector<uint8_t> quadNodes;
    quadNodes.reserve(sprites.size() + texts.size());
    for (auto& q : sprites) quadNodes.push_back(q.node);
    for (auto& q : texts)   quadNodes.push_back(q.node);

    ensureVertexBuffer(verts * sizeof(float));
    ensureIndexBuffer(idxs * sizeof(uint32_t));

    // Upload interleaved vertices (4 per quad) and indices directly into the
    // persistent GPU buffers -- no per-frame temp realloc + full copy.
    {
        void* p; vkMapMemory(vk.device, vbufMem, 0, verts * sizeof(float), 0, &p);
        memcpy(p, br.vbuf.data(), verts * sizeof(float)); vkUnmapMemory(vk.device, vbufMem);
    }
    {
        void* p; vkMapMemory(vk.device, ibufMem, 0, idxs * sizeof(uint32_t), 0, &p);
        memcpy(p, br.ibuf.data(), idxs * sizeof(uint32_t)); vkUnmapMemory(vk.device, ibufMem);
    }

    // ---- split-screen diagnostic (TOMS_SPLIT=1) ----
    // Render the frame 4x: one per quadrant, each showing ONLY quads from one node
    // (1=stage, 2=char, 3=talk, 4=battle). Each quadrant clears to a label color so an
    // empty node is obvious. This isolates which node draws a stray "unexpected" sprite.
    static int splitMode = -1;
    if (splitMode < 0) { const char* e = std::getenv("TOMS_SPLIT"); splitMode = (e && e[0]=='1') ? 1 : 0; }
    if (splitMode) {
        struct QV { int node; float r,g,b; } qv[4] = {
            {1, 0.12f,0.04f,0.18f},  // stage  (dark purple)
            {2, 0.04f,0.14f,0.18f},  // char   (dark teal)
            {3, 0.18f,0.13f,0.04f},  // talk   (dark amber)
            {4, 0.18f,0.04f,0.04f},  // battle (dark red)
        };
        uint32_t hw = W/2, hh = H/2;
        // SINGLE render pass: clear full frame once, then draw each node's quads into its
        // own viewport+scissor (with an xform that scales the node into the quadrant).
        // One pass avoids the multi-pass font-atlas descriptor bug.
        VkCommandBuffer cb = beginOnce(vk.device, cmdPool);
        VkRenderPassBeginInfo rb{}; rb.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rb.renderPass=renderPass; rb.framebuffer=swapFBs[vk.currentImageIndex]; rb.renderArea={0,0,W,H};
        VkClearValue cv{}; cv.color={0.04f,0.04f,0.07f,1.0f}; rb.clearValueCount=1; rb.pClearValues=&cv;
        vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport vp{}; vp.x=0; vp.y=0; vp.width=(float)W; vp.height=(float)H; vp.minDepth=0; vp.maxDepth=1;
        VkRect2D sc{}; sc.offset={0,0}; sc.extent={W,H};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        float pc0[8]={(float)W,(float)H, 0,0, 0,0, 1,1};
        vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 32, pc0);
        VkBuffer bufs[1]={vbuf}; VkDeviceSize off[1]={0};
        vkCmdBindVertexBuffers(cb,0,1,bufs,off);
        vkCmdBindIndexBuffer(cb, ibuf, 0, VK_INDEX_TYPE_UINT32);
        for (int qi=0; qi<4; qi++) {
            int nx=(qi%2)*(int)hw, ny=(qi/2)*(int)hh;
            // xform places the node's full-screen content into its quadrant.
            // No per-quadrant viewport/scissor: the full-screen viewport + xform is enough,
            // and per-quadrant viewport was clipping the offset quadrants to empty.
            float pc[8]={(float)W,(float)H, 0,0, (float)nx,(float)ny, 0.5f,0.5f};
            vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 32, pc);
            uint32_t globalQI=0;
            int cntPerNode[5]={0,0,0,0,0};
            for (auto& b : br.batches) {
                if (b.indexCount==0) continue;
                uint32_t nq=b.indexCount/6;
                VkDescriptorSet ds=(b.texSet==(void*)spriteSet)?spriteSet
                                  : (b.texSet==(void*)fontSet)?fontSet
                                  : b.solid?solidSet_:spriteSet;
                for (uint32_t k=0;k<nq;k++) {
                    uint8_t node=quadNodes[globalQI++];
                    if (node!=qv[qi].node) continue;
                    cntPerNode[node]++;
                    uint32_t firstIndex=b.indexOffset+k*6;
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &ds, 0, 0);
                    vkCmdDrawIndexed(cb, 6, 1, firstIndex, 0, 0);
                }
            }
            std::fprintf(stderr, "[split] qi=%d expectNode=%d drew:", qi, qv[qi].node);
            for (int n=0;n<5;n++) if (cntPerNode[n]) std::fprintf(stderr, " n%d=%d", n, cntPerNode[n]);
            std::fprintf(stderr, "\n");
        }
        vkCmdEndRenderPass(cb);
        // Submit with swapchain sync semaphores
        vkEndCommandBuffer(cb);
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo splitSI{}; splitSI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        splitSI.waitSemaphoreCount = 1;
        splitSI.pWaitSemaphores = &vk.imageAvailable[vk.currentFrame];
        splitSI.pWaitDstStageMask = &waitStage;
        splitSI.commandBufferCount = 1; splitSI.pCommandBuffers = &cb;
        splitSI.signalSemaphoreCount = 1;
        splitSI.pSignalSemaphores = &vk.renderFinished[vk.currentFrame];
        vk_check(vkQueueSubmit(vk.gfxQueue, 1, &splitSI, vk.inFlight[vk.currentFrame]), "split_submit");
        vkFreeCommandBuffers(vk.device, cmdPool, 1, &cb);
        vk.present();
        return;
    }

    // ---- Normal (non-split) windowed path ----
    uint32_t imgIdx = vk.currentImageIndex;
    VkCommandBuffer cb = cmdBufs[imgIdx];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo cbbi{}; cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);

    VkRenderPassBeginInfo rb{}; rb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rb.renderPass = renderPass; rb.framebuffer = swapFBs[imgIdx]; rb.renderArea = {0,0,W,H};
    VkClearValue cv{}; cv.color = {0.06f,0.06f,0.1f,1.0f}; rb.clearValueCount = 1; rb.pClearValues = &cv;
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // Dynamic viewport+scissor (enabled in the pipeline) must be set every pass.
    VkViewport vp{}; vp.x=0; vp.y=0; vp.width=(float)W; vp.height=(float)H; vp.minDepth=0; vp.maxDepth=1;
    VkRect2D sc{}; sc.offset={0,0}; sc.extent={W,H};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    float res[2] = {(float)W, (float)H};
    float pcData[8] = {(float)W, (float)H, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};  // res + identity xform
    vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 32, pcData);
    VkBuffer bufs[1] = {vbuf}; VkDeviceSize off[1] = {0};
    vkCmdBindVertexBuffers(cb, 0, 1, bufs, off);
    vkCmdBindIndexBuffer(cb, ibuf, 0, VK_INDEX_TYPE_UINT32);
    // one indexed draw per batch (flush on texture/blend/solid change)
    for (auto& b : br.batches) {
        if (b.indexCount == 0) continue;
        VkDescriptorSet ds = (b.texSet == (void*)spriteSet) ? spriteSet
                          : (b.texSet == (void*)fontSet)   ? fontSet
                          : b.solid                         ? solidSet_   // flat color, dummy tex
                          : spriteSet;
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &ds, 0, 0);
        vkCmdDrawIndexed(cb, b.indexCount, 1, b.indexOffset, 0, 0);
    }
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &vk.imageAvailable[vk.currentFrame];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &vk.renderFinished[vk.currentFrame];
    vk_check(vkQueueSubmit(vk.gfxQueue, 1, &si, vk.inFlight[vk.currentFrame]), "submit");
    vk.present();
}

void Renderer::savePNG(const std::string& path) {
    // savePNG is not supported in windowed (swapchain) mode.
    fprintf(stderr, "[warn] savePNG('%s') is not supported in windowed mode\n", path.c_str());
}

// (no pending PNG statics; savePNG writes directly)

VkTextureRefs Renderer::textureRefs() const {
    return VkTextureRefs{ vk.device, vk.physical, vk.gfxQueue, cmdPool,
                          dsPool, dsLayout, sampler };
}
