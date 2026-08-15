// renderer_webgpu.cpp — WebGPU (Dawn C API) implementation of IRenderer.
#include "renderer_webgpu.h"
#ifdef __EMSCRIPTEN__
#ifdef WEBGPU

// ---- async device acquisition (WebGPU requests are callback-based) ----
static WGPUAdapter  gAdapter = nullptr;
static WGPUDevice   gDevice  = nullptr;
static WGPUQueue    gQueue   = nullptr;
static bool         gReady   = false;

static void onDevice(WGPURequestDeviceStatus status, WGPUDevice device,
                     WGPUStringView msg, void* u1, void* u2) {
    (void)status; (void)msg; (void)u1; (void)u2;
    gDevice = device;
    gQueue  = wgpuDeviceGetQueue(device);
    gReady  = true;
}
static void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                      WGPUStringView msg, void* u1, void* u2) {
    (void)status; (void)msg; (void)u1; (void)u2;
    gAdapter = adapter;
    WGPUDeviceDescriptor dd = {};
    dd.label = { "toms-device", WGPU_STRLEN };
    WGPURequestDeviceCallbackInfo ci = {};
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = onDevice;
    wgpuAdapterRequestDevice(adapter, &dd, ci);
}

static const char* WGSL = R"(
struct VSIn {
  @location(0) pos: vec2f,
  @location(1) rect: vec4f,
  @location(2) uv: vec4f,
  @location(3) tint: vec4f,
  @location(4) solid: f32,
};
struct VSOut {
  @builtin(position) clip: vec4f,
  @location(0) uv: vec2f,
  @location(1) tint: vec4f,
  @location(2) @interpolate(flat) solid: f32,
};
@group(0) @binding(0) var<uniform> uRes: vec2f;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@vertex fn vs(i: VSIn) -> VSOut {
  var o: VSOut;
  let px = i.rect.xy + i.pos * i.rect.zw;
  var clip = (px / uRes) * 2.0 - vec2f(1.0, 1.0);
  clip.y = -clip.y;
  o.clip = vec4f(clip, 0.0, 1.0);
  o.uv = i.uv.xy + i.pos * (i.uv.zw - i.uv.xy);
  o.tint = i.tint;
  o.solid = i.solid;
  return o;
}
@fragment fn fs(i: VSOut) -> @location(0) vec4f {
  if (i.solid > 0.5) {
    if (i.tint.a < 0.004) { discard; }
    return i.tint;
  }
  let c = textureSample(tex, samp, i.uv);
  if (c.a < 0.01) { discard; }
  return vec4f(c.rgb * i.tint.rgb, c.a * i.tint.a);
}
)";

static void pushQuad(std::vector<float>& dst, const Quad& q) {
    // 6 vertices (2 triangles), 15 floats each: pos2 rect4 uv4 tint4 solid1
    static const float c[6][2] = {{0,0},{1,0},{0,1},{1,0},{1,1},{0,1}};
    for (int i = 0; i < 6; ++i) {
        dst.push_back(c[i][0]); dst.push_back(c[i][1]);
        dst.push_back(q.rect[0]); dst.push_back(q.rect[1]);
        dst.push_back(q.rect[2]); dst.push_back(q.rect[3]);
        dst.push_back(q.uv[0]); dst.push_back(q.uv[1]);
        dst.push_back(q.uv[2]); dst.push_back(q.uv[3]);
        dst.push_back(q.tint[0]); dst.push_back(q.tint[1]);
        dst.push_back(q.tint[2]); dst.push_back(q.tint[3]);
        dst.push_back(q.solid ? 1.0f : 0.0f);
    }
}

WebGPURenderer::WebGPURenderer() {}
WebGPURenderer::~WebGPURenderer() {}

void WebGPURenderer::init(uint32_t w, uint32_t h) {
    W_ = w; H_ = h;
    WGPUInstanceDescriptor id = {};
    instance = wgpuCreateInstance(&id);

    // canvas surface (Emscripten HTML selector)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector src = {};
    src.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    src.chain.next = nullptr;
    src.selector = { "#canvas", WGPU_STRLEN };
    WGPUSurfaceDescriptor sd = {};
    sd.nextInChain = &src.chain;
    sd.label = { "canvas", WGPU_STRLEN };
    surface = wgpuInstanceCreateSurface(instance, &sd);

    // request adapter/device asynchronously
    WGPURequestAdapterOptions opts = {};
    opts.nextInChain = nullptr;
    WGPURequestAdapterCallbackInfo ai = {};
    ai.mode = WGPUCallbackMode_AllowSpontaneous;
    ai.callback = onAdapter;
    wgpuInstanceRequestAdapter(instance, &opts, ai);
}

void WebGPURenderer::loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) {
    if (!packAtlas(layers, sw, sh, 9, spriteData, spriteW, spriteH))
        fprintf(stderr, "[webgpu] sprite pack fail\n");
    haveSprite = true;
}
void WebGPURenderer::loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    fontData.assign(px.begin(), px.end());
    fontW = w; fontH = h;
    haveFont = true;
}

void WebGPURenderer::begin() { sprites_.clear(); texts_.clear(); }

void WebGPURenderer::drawSprite(const Quad& q) { pushQuad(sprites_, q); }
void WebGPURenderer::drawText(const Quad& q)   { pushQuad(texts_, q); }

void WebGPURenderer::buildTexture(const std::vector<uint8_t>& data, uint32_t w, uint32_t h,
                                  WGPUTexture* tex, WGPUTextureView* view, WGPUBindGroup* bg) {
    WGPUTextureDescriptor td = {};
    td.label = { "atlas", WGPU_STRLEN };
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size = { w, h, 1 };
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    *tex = wgpuDeviceCreateTexture(device, &td);

    // WebGPU requires bytesPerRow aligned to 256 for writeTexture
    uint32_t stride = ((w * 4 + 255) / 256) * 256;
    std::vector<uint8_t> staged(stride * h, 0);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(staged.data() + y * stride, data.data() + y * w * 4, w * 4);

    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = *tex;
    dst.mipLevel = 0;
    dst.origin = { 0, 0, 0 };
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = stride;
    layout.rowsPerImage = h;
    WGPUExtent3D size = { w, h, 1 };
    wgpuQueueWriteTexture(queue, &dst, staged.data(), staged.size(), &layout, &size);

    WGPUTextureViewDescriptor vd = {};
    vd.label = { "atlas-view", WGPU_STRLEN };
    vd.format = WGPUTextureFormat_RGBA8Unorm;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.baseMipLevel = 0; vd.mipLevelCount = 1;
    vd.baseArrayLayer = 0; vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    *view = wgpuTextureCreateView(*tex, &vd);

    WGPUBindGroupEntry e0 = {};  // unused placeholder slot (binding 0 = ubo)
    WGPUBindGroupEntry e1 = {};
    e1.binding = 1; e1.textureView = *view;
    WGPUBindGroupEntry e2 = {};
    e2.binding = 2; e2.sampler = sampler;
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0; entries[0].buffer = ubo; entries[0].offset = 0; entries[0].size = 8;
    entries[1] = e1; entries[2] = e2;
    WGPUBindGroupDescriptor bgd = {};
    bgd.layout = bgl;
    bgd.entryCount = 3;
    bgd.entries = entries;
    *bg = wgpuDeviceCreateBindGroup(device, &bgd);
}

void WebGPURenderer::ensureBuilt() {
    if (built || !gReady) return;
    device = gDevice; queue = gQueue;
    (void)adapter;

    // bind group layout: 0=uniform(vertex), 1=texture(fragment), 2=sampler(fragment)
    WGPUBindGroupLayoutEntry e0 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    e0.binding = 0; e0.visibility = WGPUShaderStage_Vertex;
    e0.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT; e0.buffer.type = WGPUBufferBindingType_Uniform;
    WGPUBindGroupLayoutEntry e1 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    e1.binding = 1; e1.visibility = WGPUShaderStage_Fragment;
    e1.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT; e1.texture.sampleType = WGPUTextureSampleType_Float; e1.texture.viewDimension = WGPUTextureViewDimension_2D;
    WGPUBindGroupLayoutEntry e2 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    e2.binding = 2; e2.visibility = WGPUShaderStage_Fragment;
    e2.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT; e2.sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutEntry ents[3] = { e0, e1, e2 };
    WGPUBindGroupLayoutDescriptor bgld = {};
    bgld.entryCount = 3; bgld.entries = ents;
    bgl = wgpuDeviceCreateBindGroupLayout(device, &bgld);

    // shader module (WGSL)
    WGPUShaderSourceWGSL wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.chain.next = nullptr;
    wgsl.code = { WGSL, WGPU_STRLEN };
    WGPUShaderModuleDescriptor smd = {};
    smd.nextInChain = &wgsl.chain;
    smd.label = { "toms-wgsl", WGPU_STRLEN };
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(device, &smd);

    // pipeline layout
    WGPUPipelineLayoutDescriptor pld = {};
    pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &pld);

    // vertex buffer layout (15 floats / 60 bytes)
    WGPUVertexAttribute attrs[5] = {};
    attrs[0].shaderLocation = 0; attrs[0].offset = 0;   attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[1].shaderLocation = 1; attrs[1].offset = 8;   attrs[1].format = WGPUVertexFormat_Float32x4;
    attrs[2].shaderLocation = 2; attrs[2].offset = 24;  attrs[2].format = WGPUVertexFormat_Float32x4;
    attrs[3].shaderLocation = 3; attrs[3].offset = 40;  attrs[3].format = WGPUVertexFormat_Float32x4;
    attrs[4].shaderLocation = 4; attrs[4].offset = 56;  attrs[4].format = WGPUVertexFormat_Float32;
    WGPUVertexBufferLayout vbl = {};
    vbl.arrayStride = 60; vbl.attributeCount = 5; vbl.attributes = attrs;

    WGPUBlendComponent bc = WGPU_BLEND_COMPONENT_INIT;
    bc.srcFactor = WGPUBlendFactor_SrcAlpha; bc.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha; bc.operation = WGPUBlendOperation_Add;
    WGPUBlendState bs = WGPU_BLEND_STATE_INIT; bs.color = bc; bs.alpha = bc;
    WGPUColorTargetState cts = WGPU_COLOR_TARGET_STATE_INIT;
    cts.format = WGPUTextureFormat_BGRA8Unorm; cts.blend = &bs; cts.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = mod; fs.entryPoint = { "fs", WGPU_STRLEN }; fs.targetCount = 1; fs.targets = &cts;
    WGPUPrimitiveState ps = WGPU_PRIMITIVE_STATE_INIT;
    ps.topology = WGPUPrimitiveTopology_TriangleList;

    WGPURenderPipelineDescriptor rpd = {};
    rpd.layout = layout;
    rpd.vertex.module = mod; rpd.vertex.entryPoint = { "vs", WGPU_STRLEN };
    rpd.vertex.bufferCount = 1; rpd.vertex.buffers = &vbl;
    rpd.fragment = &fs;
    rpd.primitive = ps;
    pipeline = wgpuDeviceCreateRenderPipeline(device, &rpd);

    // uniform (resolution vec2)
    WGPUBufferDescriptor ubd = {};
    ubd.label = { "ubo", WGPU_STRLEN };
    ubd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubd.size = 16;  // 16-byte aligned
    ubo = wgpuDeviceCreateBuffer(device, &ubd);

    // vertex buffer (dynamic, initial 64K)
    vbufCap = 65536;
    WGPUBufferDescriptor vbd = {};
    vbd.label = { "vbuf", WGPU_STRLEN };
    vbd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vbd.size = vbufCap;
    vbuf = wgpuDeviceCreateBuffer(device, &vbd);

    // sampler
    WGPUSamplerDescriptor sd = {};
    sd.label = { "samp", WGPU_STRLEN };
    sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
    sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler = wgpuDeviceCreateSampler(device, &sd);

    // configure surface
    WGPUSurfaceConfiguration cfg = WGPU_SURFACE_CONFIGURATION_INIT;
    cfg.device = device;
    cfg.format = WGPUTextureFormat_BGRA8Unorm;
    cfg.usage = WGPUTextureUsage_RenderAttachment;
    cfg.width = W_; cfg.height = H_;
    cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(surface, &cfg);

    // textures + bind groups
    if (haveSprite) buildTexture(spriteData, spriteW, spriteH, &spriteTex, &spriteView, &spriteBG);
    if (haveFont)   buildTexture(fontData, fontW, fontH, &fontTex, &fontView, &fontBG);

    built = true;
}

void WebGPURenderer::flush(std::vector<float>& verts, WGPUBindGroup bg) {
    if (verts.empty() || !bg) return;
    size_t need = verts.size() * 4;
    if (need > vbufCap) {
        WGPUBufferDescriptor vbd = {};
        vbd.label = { "vbuf", WGPU_STRLEN };
        vbd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbd.size = need;
        WGPUBuffer nb = wgpuDeviceCreateBuffer(device, &vbd);
        wgpuBufferRelease(vbuf);
        vbuf = nb; vbufCap = need;
    }
    wgpuQueueWriteBuffer(queue, ubo, 0, &W_, 8);
    wgpuQueueWriteBuffer(queue, vbuf, 0, verts.data(), need);

    WGPUSurfaceTexture st = {};
    wgpuSurfaceGetCurrentTexture(surface, &st);
    if (!st.texture) return;
    WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);

    WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAtt.view = view;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    WGPUColor clear = { 0.0, 0.0, 0.0, 1.0 };
    colorAtt.clearValue = clear;
    WGPURenderPassColorAttachment atts[1] = { colorAtt };
    WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = atts;

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, nullptr);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, need);
    wgpuRenderPassEncoderDraw(pass, (uint32_t)(verts.size() / 15), 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(queue, 1, &cb);
    wgpuSurfacePresent(surface);
    wgpuTextureViewRelease(view);
}

void WebGPURenderer::end() {
    ensureBuilt();
    if (!built) return;  // device not ready yet
    flush(sprites_, spriteBG);
    flush(texts_, fontBG);
}

#endif // WEBGPU
#endif // __EMSCRIPTEN__
