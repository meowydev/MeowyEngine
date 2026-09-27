// meowyrender - src/backend/metal/metal_backend.mm  (internal)
//
// Metal backend (macOS / visionOS) implemented in Objective-C++.
//
// This provides a real Metal device + CAMetalLayer setup, a vertex/uniform
// buffer batch path, and texture creation. The GLFW native cocoa handle is
// used to attach a CAMetalLayer to the window's content view. The MSL shader
// source is embedded and compiled at runtime.
//
// Native desktop and visionOS windowed rendering share this implementation.
#include "backend/metal/metal_backend.hpp"
#include "core/mr_state.hpp"
#include "backend/pixel_conversion.hpp"
#include "backend/pbr_shader.hpp"
#include "meowyrender/meowyrender.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#if defined(MEOWY_PLATFORM_VISIONOS)
#import <UIKit/UIKit.h>
#else
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#endif

#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

#if defined(MEOWY_WITH_IMGUI)
#include "imgui.h"
#include "backends/imgui_impl_metal.h"
#if !defined(MEOWY_PLATFORM_VISIONOS)
// Desktop Metal drives ImGui platform/input through GLFW. visionOS has no GLFW
// and instead feeds ImGui IO from the UIKit view + engine input state below.
#include "backends/imgui_impl_glfw.h"
#endif
#endif

namespace meowyrender::backend::metal {

// Embedded Metal Shading Language: same semantics as the GL batch shader.
static const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;

// Vertex is pulled manually by [[vertex_id]] from a device buffer rather than
// via [[stage_in]] + a vertex descriptor. The visionOS simulator's GPU
// validation inflates vertex-descriptor buffer indices past its 30-slot limit
// (rejecting the pipeline with "argument index 32 > 30"); manual vertex
// pulling keeps every buffer binding at a low, explicit index. The packed
// layout mirrors backend::Vertex byte-for-byte.
struct PackedVertex {
    packed_float3 position;
    packed_float2 texcoord;
    uchar4 color;          // normalized in the shader
    packed_float4 joints;
    packed_float4 weights;
    packed_float3 normal;
};
struct VSOut {
    float4 position [[position]];
    float2 texcoord;
    float4 color;
    float3 world;
    float3 normal;
};
struct Uniforms {
    float4x4 projection;
    float4x4 modelview;
};

float3 transformNormal(float4x4 m,float3 normal) {
    float3 a=m[0].xyz,b=m[1].xyz,c=m[2].xyz;
    float determinant=dot(a,cross(b,c));
    return abs(determinant)>0.000001?(cross(b,c)*normal.x+cross(c,a)*normal.y+cross(a,b)*normal.z)/determinant:normal;
}
vertex VSOut vs_main(device const PackedVertex* verts [[buffer(0)]],
                     uint vid [[vertex_id]],
                     constant Uniforms& u [[buffer(1)]],
                     constant float4x4* instances [[buffer(3)]], uint instance [[instance_id]],
                     constant float4x4* bones [[buffer(4)]]) {
    VSOut out;
    PackedVertex in=verts[vid];
    float3 inPosition=float3(in.position);
    float3 inNormal=float3(in.normal);
    float4 inWeights=float4(in.weights);
    float4 inJoints=float4(in.joints);
    float4 position=float4(inPosition,1.0);
    float3 normal=inNormal;
    float sum=dot(inWeights,float4(1));
    if(sum>0) {
        position=float4(0);
        normal=float3(0);
        for(int i=0;i<4;++i) if(inWeights[i]>0) {
            position+=bones[uint(inJoints[i])]*float4(inPosition,1)*(inWeights[i]/sum);
            normal+=transformNormal(bones[uint(inJoints[i])],inNormal)*(inWeights[i]/sum);
        }
    }
    float4 clip = u.projection * u.modelview * instances[instance] * position;
    // The shared math produces OpenGL-style clip space (Z in [-w, w]).
    // Metal expects Z in [0, w], so remap: z' = (z + w) / 2.
    clip.z = (clip.z + clip.w) * 0.5;
    out.position = clip;
    out.world=(instances[instance]*position).xyz;
    out.normal=transformNormal(instances[instance],normal);
    out.texcoord = float2(in.texcoord);
    out.color = float4(in.color)/255.0; // uchar4 -> normalized
    return out;
}

// Depth-only vertex shader for the directional shadow pass. Applies the same
// skinning/instancing as vs_main but with the light's view-projection so the
// resulting depth texture is the scene seen from the light.
vertex float4 vs_shadow(device const PackedVertex* verts [[buffer(0)]],
                        uint vid [[vertex_id]],
                        constant Uniforms& u [[buffer(1)]],
                        constant float4x4* instances [[buffer(3)]], uint instance [[instance_id]],
                        constant float4x4* bones [[buffer(4)]]) {
    PackedVertex in=verts[vid];
    float4 position=float4(float3(in.position),1.0);
    float4 inWeights=float4(in.weights); float4 inJoints=float4(in.joints);
    float sum=dot(inWeights,float4(1));
    if(sum>0) {
        position=float4(0);
        for(int i=0;i<4;++i) if(inWeights[i]>0)
            position+=bones[uint(inJoints[i])]*float4(float3(in.position),1)*(inWeights[i]/sum);
    }
    float4 clip = u.projection * u.modelview * instances[instance] * position;
    clip.z = (clip.z + clip.w) * 0.5; // GL clip [-w,w] -> Metal [0,w]
    return clip;
}

struct LitUniforms {float4 eye,direction,radiance,ambient,emission; float metallic,roughness; uint enabled,mask; float alphaCutoff; uint hasEnv,envMips; float envIntensity; float4x4 shadowMatrix; uint hasShadow; uint cascadeCount; uint hasPrecomputed,prefilterMips; float4x4 cascadeMatrix[4]; float4 cascadeSplit; float4x4 viewMatrix;};
// PCF sample of a single depth map at a projected coordinate.
float pcfSample(depth2d<float> shadowMap, sampler shadowSamp, float3 proj, float current) {
    float2 uv = proj.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y; // Metal texture origin is top-left
    if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0||proj.z>1.0) return 1.0;
    float sum = 0.0;
    float texel = 1.0/float(shadowMap.get_width());
    for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) {
        float closest = shadowMap.sample(shadowSamp, uv + float2(x,y)*texel);
        sum += current <= closest ? 1.0 : 0.0;
    }
    return sum/9.0;
}
// 3x3 PCF directional shadow factor for the single-map path (1 = lit).
float shadowFactor(depth2d<float> shadowMap, sampler shadowSamp, float4x4 shadowMatrix, float3 world, float3 n, float3 l) {
    float4 lightClip = shadowMatrix * float4(world,1.0);
    lightClip.z = (lightClip.z + lightClip.w) * 0.5; // match GL->Metal depth remap
    float3 proj = lightClip.xyz / lightClip.w;
    float bias = max(0.0025*(1.0-dot(n,l)),0.0005);
    return pcfSample(shadowMap, shadowSamp, proj, proj.z - bias);
}
fragment float4 fs_main(VSOut in [[stage_in]],
                        texture2d<float> tex [[texture(0)]],
                        sampler samp [[sampler(0)]],
                        texture2d<float> metalMap [[texture(2)]],texture2d<float> roughMap [[texture(3)]],
                        texture2d<float> normalMap [[texture(4)]],texture2d<float> aoMap [[texture(5)]],texture2d<float> emissionMap [[texture(6)]],
                        texturecube<float> environment [[texture(7)]],
                        depth2d<float> shadowMap [[texture(8)]],
                        depth2d<float> cascade0 [[texture(9)]], depth2d<float> cascade1 [[texture(10)]],
                        depth2d<float> cascade2 [[texture(11)]], depth2d<float> cascade3 [[texture(12)]],
                        texturecube<float> irradianceMap [[texture(13)]], texturecube<float> prefilterMap [[texture(14)]],
                        texture2d<float> brdfLut [[texture(15)]],
                        constant LitUniforms& light [[buffer(5)]]) {
    float4 color=tex.sample(samp,in.texcoord)*in.color;
    // glTF alpha MASK: discard fragments below the cutoff.
    if(light.alphaCutoff>0.0 && color.a<light.alphaCutoff) discard_fragment();
    if(!light.enabled) return color;
    float3 normal=dot(in.normal,in.normal)>0.000001?safeNormal(in.normal):safeNormal(cross(dfdx(in.world),dfdy(in.world)));
    if(light.mask&4) {
        float3 tangentNormal=normalMap.sample(samp,in.texcoord).xyz*2.0-1.0;
        float3 px=dfdx(in.world),py=dfdy(in.world); float2 tx=dfdx(in.texcoord),ty=dfdy(in.texcoord);
        float determinant=tx.x*ty.y-tx.y*ty.x;
        if(abs(determinant)>0.000001) {
            float3 tangent=safeNormal((px*ty.y-py*tx.y)/determinant),bitangent=safeNormal((py*tx.x-px*ty.x)/determinant);
            normal=safeNormal(tangent*tangentNormal.x+bitangent*tangentNormal.y+normal*tangentNormal.z);
        }
    }
    float metallic=light.metallic*((light.mask&1)?metalMap.sample(samp,in.texcoord).b:1.0);
    float roughness=light.roughness*((light.mask&2)?roughMap.sample(samp,in.texcoord).g:1.0);
    float ao=(light.mask&8)?aoMap.sample(samp,in.texcoord).r:1.0;
    float3 emission=light.emission.xyz*((light.mask&16)?pow(emissionMap.sample(samp,in.texcoord).rgb,float3(2.2)):float3(1));
    float3 baseLinear=pow(max(color.rgb,float3(0)),float3(2.2));
    // Directional shadow factor (1 = lit) modulates the direct radiance.
    float shadow=1.0;
    constexpr sampler shadowSamp(mag_filter::linear,min_filter::linear,address::clamp_to_edge);
    if(light.cascadeCount>0u) {
        // Select the cascade by the fragment's view-space depth.
        float viewDepth = -(light.viewMatrix * float4(in.world,1.0)).z;
        uint idx = light.cascadeCount-1u;
        for(uint c=0;c<light.cascadeCount;++c) if(viewDepth <= light.cascadeSplit[c]) { idx=c; break; }
        float4 lightClip = light.cascadeMatrix[idx] * float4(in.world,1.0);
        lightClip.z = (lightClip.z + lightClip.w) * 0.5;
        float3 proj = lightClip.xyz / lightClip.w;
        float bias = max(0.0025*(1.0-dot(normal,safeNormal(-light.direction.xyz))),0.0005);
        float current = proj.z - bias;
        shadow = idx==0u?pcfSample(cascade0,shadowSamp,proj,current):
                 idx==1u?pcfSample(cascade1,shadowSamp,proj,current):
                 idx==2u?pcfSample(cascade2,shadowSamp,proj,current):pcfSample(cascade3,shadowSamp,proj,current);
    } else if(light.hasShadow) {
        shadow=shadowFactor(shadowMap,shadowSamp,light.shadowMatrix,in.world,normal,safeNormal(-light.direction.xyz));
    }
    float3 directRadiance=light.radiance.xyz*shadow;
    if(light.hasPrecomputed) {
        // Precomputed split-sum IBL: irradiance (diffuse), prefiltered specular
        // via roughness LOD, and the BRDF integration LUT.
        float3 view=safeNormal(light.eye.xyz-in.world);
        float3 refl=reflect(-view,normal);
        constexpr sampler cubeSampler(mag_filter::linear,min_filter::linear,mip_filter::linear,address::clamp_to_edge);
        constexpr sampler lutSampler(mag_filter::linear,min_filter::linear,address::clamp_to_edge);
        float3 irradiance=pow(irradianceMap.sample(cubeSampler,normal).rgb,float3(2.2))*light.envIntensity;
        float lod=roughness*float(max(int(light.prefilterMips)-1,0));
        float3 prefiltered=pow(prefilterMap.sample(cubeSampler,refl,level(lod)).rgb,float3(2.2))*light.envIntensity;
        float2 brdf=brdfLut.sample(lutSampler,float2(max(dot(normal,view),0.0),roughness)).rg;
        return float4(shadePBRSplitSum(baseLinear,normal,in.world,light.eye.xyz,light.direction.xyz,directRadiance,emission,metallic,roughness,ao,irradiance,prefiltered,brdf),color.a);
    }
    if(light.hasEnv) {
        float3 view=safeNormal(light.eye.xyz-in.world);
        float3 refl=reflect(-view,normal);
        constexpr sampler cubeSampler(mag_filter::linear,min_filter::linear,mip_filter::linear,address::clamp_to_edge);
        float3 envDiffuse=pow(environment.sample(cubeSampler,normal).rgb,float3(2.2))*light.envIntensity;
        float lod=roughness*float(max(int(light.envMips)-1,0));
        float3 envSpecular=pow(environment.sample(cubeSampler,refl,level(lod)).rgb,float3(2.2))*light.envIntensity;
        return float4(shadePBRIBL(baseLinear,normal,in.world,light.eye.xyz,light.direction.xyz,directRadiance,emission,metallic,roughness,ao,envDiffuse,envSpecular),color.a);
    }
    return float4(shadePBR(baseLinear,normal,in.world,light.eye.xyz,light.direction.xyz,directRadiance,light.ambient.xyz,emission,metallic,roughness,ao),color.a);
}
kernel void decode_texture(texture2d<float, access::sample> source [[texture(0)]],
                           texture2d<float, access::write> destination [[texture(1)]],
                           uint2 pixel [[thread_position_in_grid]]) {
    if(pixel.x>=destination.get_width() || pixel.y>=destination.get_height()) return;
    constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest);
    float2 uv=(float2(pixel)+0.5)/float2(destination.get_width(),destination.get_height());
    destination.write(source.sample(point,uv,level(0)),pixel);
}
)";

struct MetalBackend::Impl {
    struct Framebuffer {
        id<MTLTexture> color = nil;
        id<MTLTexture> depth = nil;
        int width = 0;
        int height = 0;
        unsigned int colorTextureId = 0;
        id<MTLTexture> multisample = nil;
    };
    struct UniformBinding {
        std::string name;
        NSUInteger offset = 0;
        NSUInteger size = 0;
        NSUInteger count=1,stride=0;
        int type=-1;
    };
    struct ShaderProgram {
        id<MTLLibrary> library = nil;
        id<MTLRenderPipelineState> pipeline = nil;
        MTLRenderPipelineDescriptor* descriptor = nil;
        std::unordered_map<int, id<MTLRenderPipelineState>> blendPipelines;
        std::vector<UniformBinding> uniforms;
        std::vector<std::uint8_t> uniformData;
    };

    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    CAMetalLayer* layer = nil;
#if defined(MEOWY_PLATFORM_VISIONOS)
    // The UIKit render view (owns the CAMetalLayer). Kept so the visionOS ImGui
    // path can read logical size + display scale for ImGui IO, since there is
    // no GLFW platform backend to supply them.
    UIView* view = nil;
#endif
    id<MTLLibrary> library = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    MTLRenderPipelineDescriptor* descriptor = nil;
    std::unordered_map<int, id<MTLRenderPipelineState>> blendPipelines;
    int blendMode = 0;
    std::vector<Matrix> instances;
    std::vector<Matrix> bones;
    Surface surface;
    // Persistent GPU mesh cache. When boundMeshBuffer is set, DrawVertices uses
    // it instead of allocating a transient vertex buffer from CPU data.
    std::unordered_map<unsigned int, id<MTLBuffer>> meshBuffers;
    unsigned int nextMeshBuffer = 1;
    id<MTLBuffer> boundMeshBuffer = nil;
    bool scissorEnabled = false;
    int scissorX=0, scissorY=0, scissorW=0, scissorH=0;
    id<MTLSamplerState> sampler = nil;
    id<MTLDepthStencilState> depthStateOn = nil;
    id<MTLDepthStencilState> depthStateOff = nil;
    id<MTLDepthStencilState> depthStateTestNoWrite = nil; // test LEQUAL, no write (transparent)
    bool depthMask = true;  // independent depth-write toggle (SetDepthMask)
    id<MTLTexture> depthTexture = nil;
    id<MTLTexture> multisampleColor = nil;
    NSUInteger samples = 1;

    // Directional shadow mapping: a depth-only pipeline renders occluders into
    // shadowTexture from the light's POV; the lit shader samples it (slot 8).
    id<MTLRenderPipelineState> shadowPipeline = nil;
    id<MTLTexture> shadowTexture = nil;
    id<MTLSamplerState> shadowSampler = nil;
    int shadowResolution = 0;
    bool shadowPass = false;      // currently rendering the depth pass
    bool hasShadowMap = false;    // shadowTexture holds a valid map for lit draws
    Matrix shadowMatrix;          // light view-projection
    // Cascaded shadow maps (up to 4 depth textures + matrices + splits).
    static constexpr int kMaxCascades = 4;
    id<MTLTexture> cascadeTexture[kMaxCascades] = {nil,nil,nil,nil};
    Matrix cascadeMatrix[kMaxCascades]{};
    float cascadeSplit[kMaxCascades] = {0,0,0,0};
    Matrix cascadeViewMatrix{};
    int cascadeCount = 0;         // >0 when cascaded shadows active for lit draws
    int cascadeResolution = 0;
    int activeCascade = -1;       // cascade currently recording its depth pass

    id<CAMetalDrawable> currentDrawable = nil;
    id<MTLCommandBuffer> currentCmd = nil;
    id<MTLRenderCommandEncoder> currentEncoder = nil;
    bool depthEnabled = false;

    Matrix projection;
    Rectangle viewport{0,0,1,1};
    Matrix modelview;
    Color clearColor{0, 0, 0, 255};

    std::unordered_map<unsigned int, id<MTLTexture>> textures;
    std::unordered_map<unsigned int, id<MTLSamplerState>> textureSamplers;
    std::unordered_map<unsigned int, int> textureFilters;
    std::unordered_map<unsigned int, int> textureWraps;
    std::unordered_map<unsigned int, Framebuffer> framebuffers;
    unsigned int nextTextureId = 1;
    unsigned int nextFramebufferId = 1;
    unsigned int whiteTex = 0;
    unsigned int whiteCube = 0; // 1x1 white cubemap, IBL fallback when disabled
    unsigned int activeFramebuffer = 0;
    std::unordered_map<unsigned int, ShaderProgram> shaders;
    unsigned int nextShaderId = 1;
    unsigned int activeShader = 0;

    int width = 0;
    int height = 0;
};

namespace {
id<MTLTexture> MultisampleColor(id<MTLDevice> device,int width,int height,NSUInteger samples) {
    if(samples==1) return nil;
    auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
    descriptor.textureType=MTLTextureType2DMultisample; descriptor.sampleCount=samples;
    descriptor.usage=MTLTextureUsageRenderTarget; descriptor.storageMode=MTLStorageModePrivate;
    return [device newTextureWithDescriptor:descriptor];
}

template<typename T, typename P>
id<MTLRenderPipelineState> BlendPipeline(T* impl, P& program) {
    auto it=program.blendPipelines.find(impl->blendMode);
    if(it!=program.blendPipelines.end()) return it->second;
    MTLRenderPipelineDescriptor* descriptor=[program.descriptor copy];
    auto attachment=descriptor.colorAttachments[0];
    attachment.sourceRGBBlendFactor=impl->blendMode==2?MTLBlendFactorDestinationColor:MTLBlendFactorSourceAlpha;
    attachment.destinationRGBBlendFactor=impl->blendMode==1?MTLBlendFactorOne:(impl->blendMode==2?MTLBlendFactorZero:MTLBlendFactorOneMinusSourceAlpha);
    // Match the existing public blend modes, including alpha-channel factors.
    attachment.sourceAlphaBlendFactor=impl->blendMode==2?MTLBlendFactorDestinationAlpha:MTLBlendFactorSourceAlpha;
    attachment.destinationAlphaBlendFactor=attachment.destinationRGBBlendFactor;
    NSError* error=nil;
    auto pipeline=[impl->device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if(!pipeline) throw std::runtime_error(error?error.localizedDescription.UTF8String:"Metal blend pipeline failed");
    program.blendPipelines.emplace(impl->blendMode,pipeline);
    return pipeline;
}

NSUInteger MetalDataTypeSize(MTLDataType type) {
    switch (type) {
        case MTLDataTypeFloat: return sizeof(float);
        case MTLDataTypeFloat2: return sizeof(float) * 2;
        case MTLDataTypeFloat3: return sizeof(float) * 3;
        case MTLDataTypeFloat4: return sizeof(float) * 4;
        case MTLDataTypeFloat4x4:return sizeof(float)*16;
        case MTLDataTypeInt: return sizeof(std::int32_t);
        case MTLDataTypeInt2: return sizeof(std::int32_t) * 2;
        case MTLDataTypeInt3: return sizeof(std::int32_t) * 3;
        case MTLDataTypeInt4: return sizeof(std::int32_t) * 4;
        default: return 0;
    }
}

template <typename T>
void EndEncoding(T* impl) {
    if (impl->currentEncoder) {
        [impl->currentEncoder endEncoding];
        impl->currentEncoder = nil;
    }
}

template <typename T>
id<MTLTexture> ActiveColorTexture(T* impl) {
    if (impl->activeFramebuffer == 0) {
        return impl->currentDrawable ? impl->currentDrawable.texture : nil;
    }
    const auto it = impl->framebuffers.find(impl->activeFramebuffer);
    return it == impl->framebuffers.end() ? nil : it->second.color;
}

template <typename T>
id<MTLTexture> ActiveDepthTexture(T* impl) {
    if (impl->activeFramebuffer == 0) return impl->depthTexture;
    const auto it = impl->framebuffers.find(impl->activeFramebuffer);
    return it == impl->framebuffers.end() ? nil : it->second.depth;
}

template <typename T>
std::pair<int, int> ActiveTargetSize(T* impl) {
    if (impl->activeFramebuffer == 0) return {impl->width, impl->height};
    const auto it = impl->framebuffers.find(impl->activeFramebuffer);
    if (it == impl->framebuffers.end()) return {impl->width, impl->height};
    return {it->second.width, it->second.height};
}

template <typename T>
void BeginEncoding(T* impl, MTLLoadAction loadAction,
                   Color clearColor) {
    if (impl->currentEncoder || !impl->currentCmd) return;

    id<MTLTexture> color = ActiveColorTexture(impl);
    id<MTLTexture> depth = ActiveDepthTexture(impl);
    if (!color) return;

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = loadAction;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    if(impl->samples>1) {
        rp.colorAttachments[0].texture=impl->activeFramebuffer?impl->framebuffers.at(impl->activeFramebuffer).multisample:impl->multisampleColor;
        rp.colorAttachments[0].resolveTexture=color;
        rp.colorAttachments[0].storeAction=MTLStoreActionStoreAndMultisampleResolve;
    }
    if (loadAction == MTLLoadActionClear) {
        rp.colorAttachments[0].clearColor = MTLClearColorMake(
            clearColor.r / 255.0, clearColor.g / 255.0,
            clearColor.b / 255.0, clearColor.a / 255.0);
    }
    rp.depthAttachment.texture = depth;
    rp.depthAttachment.loadAction = loadAction;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;

    impl->currentEncoder = [impl->currentCmd renderCommandEncoderWithDescriptor:rp];
    [impl->currentEncoder setRenderPipelineState:impl->pipeline];
    [impl->currentEncoder setFragmentSamplerState:impl->sampler atIndex:0];
    [impl->currentEncoder setDepthStencilState:
        impl->depthEnabled ? (impl->depthMask ? impl->depthStateOn : impl->depthStateTestNoWrite)
                           : impl->depthStateOff];
}

} // namespace

MetalBackend::MetalBackend() : impl_(new Impl()) {}
MetalBackend::~MetalBackend() { delete impl_; }

bool MetalBackend::Init(const ContextConfig& config) {
    impl_->width = config.width;
    impl_->height = config.height;

    impl_->device = MTLCreateSystemDefaultDevice();
    if (impl_->device == nil) {
        std::fprintf(stderr, "[meowyrender][Metal] no Metal device available\n");
        return false;
    }
    impl_->queue = [impl_->device newCommandQueue];
    impl_->samples=(config.configFlags & FLAG_MSAA_4X_HINT) && [impl_->device supportsTextureSampleCount:4]?4:1;

    // Attach a CAMetalLayer to the GLFW cocoa window's content view.
#if defined(MEOWY_PLATFORM_VISIONOS)
    UIView* view=(__bridge UIView*)config.window;
    impl_->view=view;
    impl_->layer=static_cast<CAMetalLayer*>(view.layer);
#else
    NSWindow* nsWindow = glfwGetCocoaWindow(static_cast<GLFWwindow*>(config.window));
    impl_->layer = [CAMetalLayer layer];
#endif
    impl_->layer.device = impl_->device;
    impl_->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    impl_->layer.framebufferOnly = NO;
    impl_->layer.drawableSize = CGSizeMake(config.width, config.height);
#if !defined(MEOWY_PLATFORM_VISIONOS)
    impl_->layer.contentsScale=nsWindow.backingScaleFactor;
    nsWindow.contentView.layer = impl_->layer;
    nsWindow.contentView.wantsLayer = YES;
#endif

    // Compile the embedded shader library.
    NSError* err = nil;
    std::string shaderSource=kMSL;
    shaderSource.insert(shaderSource.find("struct LitUniforms"),std::string("\n#define vec2 float2\n#define vec3 float3\n")+PbrFunctions+"\n#undef vec3\n#undef vec2\n");
    NSString* src = [NSString stringWithUTF8String:shaderSource.c_str()];
    impl_->library = [impl_->device newLibraryWithSource:src options:nil error:&err];
    if (impl_->library == nil) {
        std::fprintf(stderr, "[meowyrender][Metal] shader compile failed: %s\n",
                     err ? err.localizedDescription.UTF8String : "unknown");
        return false;
    }

    // A sampler for texture reads.
    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    impl_->sampler = [impl_->device newSamplerStateWithDescriptor:sd];

    // 1x1 white texture for untextured draws.
    const unsigned char white[4] = {255, 255, 255, 255};
    impl_->whiteTex = CreateTexture(white, 1, 1, PixelFormat::Uncompressed_R8G8B8A8);
    // 1x1 white cubemap bound to the IBL slot when no environment is set, so the
    // texturecube argument always has a matching texture type for validation.
    const unsigned char whiteFaces[6*4] = {255,255,255,255, 255,255,255,255, 255,255,255,255,
                                           255,255,255,255, 255,255,255,255, 255,255,255,255};
    impl_->whiteCube = CreateCubemap(whiteFaces, 1);

    // Build the render pipeline state (vs_main + fs_main, alpha blending).
    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = [impl_->library newFunctionWithName:@"vs_main"];
    pd.fragmentFunction = [impl_->library newFunctionWithName:@"fs_main"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    pd.rasterSampleCount=impl_->samples;

    // No MTLVertexDescriptor: vs_main pulls vertices from a device buffer via
    // [[vertex_id]]. This avoids the visionOS simulator's vertex-descriptor
    // buffer-index inflation (GPU validation "argument index 32 > 30").

    impl_->pipeline = [impl_->device newRenderPipelineStateWithDescriptor:pd error:&err];
    impl_->descriptor = pd;
    if (impl_->pipeline == nil) {
        std::fprintf(stderr, "[meowyrender][Metal] pipeline creation failed: %s\n",
                     err ? err.localizedDescription.UTF8String : "unknown");
        return false;
    }

    // Depth-stencil states: one that tests+writes depth (3D), one that doesn't (2D).
    MTLDepthStencilDescriptor* dsOn = [[MTLDepthStencilDescriptor alloc] init];
    dsOn.depthCompareFunction = MTLCompareFunctionLessEqual;
    dsOn.depthWriteEnabled = YES;
    impl_->depthStateOn = [impl_->device newDepthStencilStateWithDescriptor:dsOn];
    MTLDepthStencilDescriptor* dsOff = [[MTLDepthStencilDescriptor alloc] init];
    dsOff.depthCompareFunction = MTLCompareFunctionAlways;
    dsOff.depthWriteEnabled = NO;
    impl_->depthStateOff = [impl_->device newDepthStencilStateWithDescriptor:dsOff];
    // Depth test (LEQUAL) but no depth write: transparent geometry.
    MTLDepthStencilDescriptor* dsTestNoWrite = [[MTLDepthStencilDescriptor alloc] init];
    dsTestNoWrite.depthCompareFunction = MTLCompareFunctionLessEqual;
    dsTestNoWrite.depthWriteEnabled = NO;
    impl_->depthStateTestNoWrite = [impl_->device newDepthStencilStateWithDescriptor:dsTestNoWrite];

    // Depth-only pipeline for the directional shadow pass (vs_shadow, no color).
    MTLRenderPipelineDescriptor* sp = [[MTLRenderPipelineDescriptor alloc] init];
    sp.vertexFunction = [impl_->library newFunctionWithName:@"vs_shadow"];
    sp.fragmentFunction = nil;  // depth-only
    sp.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    impl_->shadowPipeline = [impl_->device newRenderPipelineStateWithDescriptor:sp error:&err];
    if (impl_->shadowPipeline == nil) {
        std::fprintf(stderr, "[meowyrender][Metal] shadow pipeline failed: %s\n",
                     err ? err.localizedDescription.UTF8String : "unknown");
        return false;
    }
    // Shadow-map sampler: clamp so out-of-frustum lookups read the far border.
    MTLSamplerDescriptor* ss = [[MTLSamplerDescriptor alloc] init];
    ss.minFilter = MTLSamplerMinMagFilterLinear;
    ss.magFilter = MTLSamplerMinMagFilterLinear;
    ss.sAddressMode = MTLSamplerAddressModeClampToEdge;
    ss.tAddressMode = MTLSamplerAddressModeClampToEdge;
    impl_->shadowSampler = [impl_->device newSamplerStateWithDescriptor:ss];
    // A 1x1 depth texture keeps slot 8 (depth2d) always bound with a valid type
    // for GPU validation when no shadow pass has run (hasShadow==0 ignores it).
    {
        MTLTextureDescriptor* std1 = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:1 height:1 mipmapped:NO];
        std1.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        std1.storageMode = MTLStorageModePrivate;
        impl_->shadowTexture = [impl_->device newTextureWithDescriptor:std1];
        impl_->shadowResolution = 1;
    }

    // Depth texture sized to the drawable.
    MTLTextureDescriptor* dtd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:config.width height:config.height
                                 mipmapped:NO];
    dtd.sampleCount=impl_->samples;
    if(impl_->samples>1) dtd.textureType=MTLTextureType2DMultisample;
    dtd.usage = MTLTextureUsageRenderTarget;
    dtd.storageMode = MTLStorageModePrivate;
    impl_->depthTexture = [impl_->device newTextureWithDescriptor:dtd];
    impl_->multisampleColor=MultisampleColor(impl_->device,config.width,config.height,impl_->samples);

    std::printf("[meowyrender][Metal] device: %s\n",
                impl_->device.name.UTF8String);
    return true;
}

void MetalBackend::Shutdown() {
    EndEncoding(impl_);
    impl_->shaders.clear();
    impl_->framebuffers.clear();
    impl_->textureSamplers.clear();
    impl_->textureFilters.clear();
    impl_->textureWraps.clear();
    for (auto& [id, tex] : impl_->textures) tex = nil;
    impl_->textures.clear();
    impl_->pipeline = nil;
    impl_->blendPipelines.clear();
    impl_->descriptor = nil;
    impl_->depthStateOn = nil;
    impl_->depthStateOff = nil;
    impl_->depthStateTestNoWrite = nil;
    impl_->depthTexture = nil;
    impl_->multisampleColor = nil;
    impl_->library = nil;
    impl_->queue = nil;
    impl_->device = nil;
}

void MetalBackend::Resize(int width, int height) {
    if(width<=0 || height<=0) return;
    impl_->width = width;
    impl_->height = height;
    impl_->layer.drawableSize = CGSizeMake(width, height);
    // Recreate the depth texture to match the new drawable size.
    MTLTextureDescriptor* dtd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:width height:height mipmapped:NO];
    dtd.sampleCount=impl_->samples;
    if(impl_->samples>1) dtd.textureType=MTLTextureType2DMultisample;
    dtd.usage = MTLTextureUsageRenderTarget;
    dtd.storageMode = MTLStorageModePrivate;
    impl_->depthTexture = [impl_->device newTextureWithDescriptor:dtd];
    impl_->multisampleColor=MultisampleColor(impl_->device,width,height,impl_->samples);
}

void MetalBackend::BeginFrame() {
    impl_->currentDrawable = [impl_->layer nextDrawable];
    impl_->currentCmd = [impl_->queue commandBuffer];
    impl_->activeFramebuffer = 0;
}

void MetalBackend::Clear(Color color) {
    impl_->clearColor = color;
    EndEncoding(impl_);
    BeginEncoding(impl_, MTLLoadActionClear, color);
}

void MetalBackend::EndFrame() {
    EndEncoding(impl_);
    if (impl_->currentDrawable)
        [impl_->currentCmd presentDrawable:impl_->currentDrawable];
    [impl_->currentCmd commit];
    impl_->currentEncoder = nil;
    impl_->currentDrawable = nil;
    impl_->currentCmd = nil;
}

void MetalBackend::SetProjection(const Matrix& p) { impl_->projection = p; }
void MetalBackend::SetViewport(Rectangle viewport) {impl_->viewport=viewport;}
void MetalBackend::SetModelview(const Matrix& m) { impl_->modelview = m; }

void MetalBackend::SetScissor(bool enabled, int x, int y, int w, int h) {
    impl_->scissorEnabled=enabled;
    impl_->scissorX=x; impl_->scissorY=y; impl_->scissorW=w; impl_->scissorH=h;
}

void MetalBackend::SetBlendMode(int mode) {
    impl_->blendMode=std::clamp(mode,0,2);
}

void MetalBackend::SetDepthTest(bool enabled) {
    impl_->depthEnabled = enabled;
    if (impl_->currentEncoder) {
        [impl_->currentEncoder setDepthStencilState:
            enabled ? (impl_->depthMask ? impl_->depthStateOn : impl_->depthStateTestNoWrite)
                    : impl_->depthStateOff];
    }
}
void MetalBackend::SetDepthMask(bool enabled) {
    impl_->depthMask = enabled;
    if (impl_->currentEncoder && impl_->depthEnabled) {
        [impl_->currentEncoder setDepthStencilState:
            enabled ? impl_->depthStateOn : impl_->depthStateTestNoWrite];
    }
}

void MetalBackend::DrawVertices(const Vertex* verts, std::size_t count,
                                DrawMode mode, unsigned int textureId) {
    if (count == 0) return;

    // Shadow depth pass: the encoder + shadow pipeline are already bound by
    // BeginShadowPass. Feed only the vertex inputs vs_shadow needs and draw.
    if (impl_->shadowPass) {
        if (!impl_->currentEncoder) return;
        struct Uniforms { Matrix projection; Matrix modelview; };
        Uniforms u{MatrixTranspose(impl_->projection), MatrixTranspose(impl_->modelview)};
        id<MTLBuffer> vb = impl_->boundMeshBuffer;
        if (!vb) vb = [impl_->device newBufferWithBytes:verts length:count*sizeof(Vertex) options:MTLResourceStorageModeShared];
        if (!vb) return;
        [impl_->currentEncoder setVertexBuffer:vb offset:0 atIndex:0];
        [impl_->currentEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        const Matrix identity=MatrixTranspose(MatrixIdentity());
        id<MTLBuffer> instanceBuffer=[impl_->device newBufferWithBytes:impl_->instances.empty()?&identity:impl_->instances.data()
            length:impl_->instances.empty()?sizeof(Matrix):impl_->instances.size()*sizeof(Matrix) options:MTLResourceStorageModeShared];
        [impl_->currentEncoder setVertexBuffer:instanceBuffer offset:0 atIndex:3];
        id<MTLBuffer> boneBuffer=[impl_->device newBufferWithBytes:impl_->bones.empty()?&identity:impl_->bones.data()
            length:impl_->bones.empty()?sizeof(Matrix):impl_->bones.size()*sizeof(Matrix) options:MTLResourceStorageModeShared];
        [impl_->currentEncoder setVertexBuffer:boneBuffer offset:0 atIndex:4];
        [impl_->currentEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count instanceCount:impl_->instances.empty()?1:impl_->instances.size()];
        return;
    }

    BeginEncoding(impl_, MTLLoadActionLoad, impl_->clearColor);
    if (!impl_->currentEncoder) return;
    const auto [width,height]=ActiveTargetSize(impl_);
    const auto v=impl_->viewport;
    [impl_->currentEncoder setViewport:MTLViewport{v.x*width,v.y*height,v.width*width,v.height*height,0,1}];
    MTLScissorRect scissor{0,0,static_cast<NSUInteger>(width),static_cast<NSUInteger>(height)};
    if(impl_->scissorEnabled) {
        double sx=impl_->activeFramebuffer?1.0:static_cast<double>(width)/std::max(1,detail::State().screenWidth);
        double sy=impl_->activeFramebuffer?1.0:static_cast<double>(height)/std::max(1,detail::State().screenHeight);
        const auto x=static_cast<long long>(std::clamp(impl_->scissorX*sx,0.0,static_cast<double>(width)));
        const auto y=static_cast<long long>(std::clamp(impl_->scissorY*sy,0.0,static_cast<double>(height)));
        const auto right=static_cast<long long>(std::clamp((static_cast<double>(impl_->scissorX)+impl_->scissorW)*sx,static_cast<double>(x),static_cast<double>(width)));
        const auto bottom=static_cast<long long>(std::clamp((static_cast<double>(impl_->scissorY)+impl_->scissorH)*sy,static_cast<double>(y),static_cast<double>(height)));
        if(right==x || bottom==y) return;
        scissor={static_cast<NSUInteger>(x),static_cast<NSUInteger>(y),static_cast<NSUInteger>(right-x),static_cast<NSUInteger>(bottom-y)};
    }
    [impl_->currentEncoder setScissorRect:scissor];

    auto shaderIt = impl_->shaders.find(impl_->activeShader);
    if (shaderIt != impl_->shaders.end()) {
        [impl_->currentEncoder setRenderPipelineState:BlendPipeline(impl_,shaderIt->second)];
        if (!shaderIt->second.uniformData.empty()) {
            auto& data=shaderIt->second.uniformData;
            id<MTLBuffer> buffer=[impl_->device newBufferWithBytes:data.data() length:data.size() options:MTLResourceStorageModeShared];
            [impl_->currentEncoder setFragmentBuffer:buffer offset:0 atIndex:2];
            [impl_->currentEncoder setVertexBuffer:buffer offset:0 atIndex:2];
        }
    } else {
        [impl_->currentEncoder setRenderPipelineState:BlendPipeline(impl_,*impl_)];
    }

    struct Uniforms { Matrix projection; Matrix modelview; };
    Uniforms u{MatrixTranspose(impl_->projection), MatrixTranspose(impl_->modelview)};

    id<MTLBuffer> vertexBuffer = impl_->boundMeshBuffer;
    if (!vertexBuffer) {
        const NSUInteger vertexBytes = count * sizeof(Vertex);
        vertexBuffer = [impl_->device newBufferWithBytes:verts
                                                  length:vertexBytes
                                                 options:MTLResourceStorageModeShared];
    }
    if (!vertexBuffer) return;
    [impl_->currentEncoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
    [impl_->currentEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    const Matrix identity=MatrixTranspose(MatrixIdentity());
    id<MTLBuffer> instanceBuffer=[impl_->device newBufferWithBytes:impl_->instances.empty()?&identity:impl_->instances.data()
        length:impl_->instances.empty()?sizeof(Matrix):impl_->instances.size()*sizeof(Matrix) options:MTLResourceStorageModeShared];
    [impl_->currentEncoder setVertexBuffer:instanceBuffer offset:0 atIndex:3];
    id<MTLBuffer> boneBuffer=[impl_->device newBufferWithBytes:impl_->bones.empty()?&identity:impl_->bones.data()
        length:impl_->bones.empty()?sizeof(Matrix):impl_->bones.size()*sizeof(Matrix) options:MTLResourceStorageModeShared];
    [impl_->currentEncoder setVertexBuffer:boneBuffer offset:0 atIndex:4];

    id<MTLTexture> tex = nil;
    auto it = impl_->textures.find(textureId ? textureId : impl_->whiteTex);
    if (it != impl_->textures.end()) tex = it->second;
    [impl_->currentEncoder setFragmentTexture:tex atIndex:0];
    if(!impl_->activeShader) {
        const auto& surface=impl_->surface;
        // Trailing fields keep the struct a 16-byte multiple matching the MSL
        // alignment (alphaCutoff + hasEnv + envMips + envIntensity fill one slot).
        struct LitUniforms {Vector4 eye,direction,radiance,ambient,emission; float metallic,roughness; unsigned int enabled,mask; float alphaCutoff; unsigned int hasEnv,envMips; float envIntensity; Matrix shadowMatrix; unsigned int hasShadow; unsigned int cascadeCount; unsigned int hasPrecomputed,prefilterMips; Matrix cascadeMatrix[4]; Vector4 cascadeSplit; Matrix viewMatrix;};
        auto vector=[](Vector3 v){return Vector4{v.x,v.y,v.z,0};};
        const bool hasPrecomputed=surface.light.irradiance&&surface.light.prefilter&&surface.light.brdfLut;
        LitUniforms light{vector(surface.light.eye),vector(surface.light.direction),vector(surface.light.radiance),vector(surface.light.ambient),vector(surface.emission),surface.metallic,surface.roughness,surface.enabled?1u:0u,surface.mask,surface.alphaCutoff,surface.light.environment?1u:0u,static_cast<unsigned int>(surface.light.environmentMips),surface.light.environmentIntensity,
                          MatrixTranspose(impl_->shadowMatrix),impl_->hasShadowMap?1u:0u,static_cast<unsigned int>(impl_->cascadeCount),hasPrecomputed?1u:0u,static_cast<unsigned int>(surface.light.prefilterMips),{},{},{}};
        for(int i=0;i<4;++i) light.cascadeMatrix[i]=MatrixTranspose(impl_->cascadeMatrix[i]);
        light.cascadeSplit={impl_->cascadeSplit[0],impl_->cascadeSplit[1],impl_->cascadeSplit[2],impl_->cascadeSplit[3]};
        light.viewMatrix=MatrixTranspose(impl_->cascadeViewMatrix);
        [impl_->currentEncoder setFragmentBytes:&light length:sizeof(light) atIndex:5];
        for(int i=0;i<5;++i) {
            auto found=impl_->textures.find(surface.maps[i]);
            [impl_->currentEncoder setFragmentTexture:found==impl_->textures.end()?impl_->textures[impl_->whiteTex]:found->second atIndex:i+2];
        }
        // Environment cubemap for IBL at texture slot 7 (white cube when off, so
        // the texturecube binding always has a matching type).
        auto env=impl_->textures.find(surface.light.environment);
        auto fallbackCube=impl_->textures.find(impl_->whiteCube);
        id<MTLTexture> envTex=(surface.light.environment&&env!=impl_->textures.end())?env->second:(fallbackCube!=impl_->textures.end()?fallbackCube->second:nil);
        [impl_->currentEncoder setFragmentTexture:envTex atIndex:7];
        // Directional shadow map at slot 8. Always bind a valid depth2d (the
        // 1x1 fallback when no shadow pass has run); the shader gates on hasShadow.
        [impl_->currentEncoder setFragmentTexture:impl_->shadowTexture atIndex:8];
        // Cascade shadow maps at slots 9..12 (fall back to the 1x1 shadow
        // texture so every depth2d binding is valid for GPU validation).
        for(int i=0;i<Impl::kMaxCascades;++i)
            [impl_->currentEncoder setFragmentTexture:(impl_->cascadeTexture[i]?impl_->cascadeTexture[i]:impl_->shadowTexture) atIndex:9+i];
        // Precomputed split-sum IBL maps at slots 13/14/15. Bind matching-type
        // fallbacks (white cube / white 2D) when absent so every texture
        // argument is valid under Metal shader validation.
        auto findTex=[&](unsigned int texId,unsigned int fallback){
            auto f=impl_->textures.find(texId?texId:fallback);
            return f!=impl_->textures.end()?f->second:(id<MTLTexture>)nil;
        };
        [impl_->currentEncoder setFragmentTexture:findTex(hasPrecomputed?surface.light.irradiance:0,impl_->whiteCube) atIndex:13];
        [impl_->currentEncoder setFragmentTexture:findTex(hasPrecomputed?surface.light.prefilter:0,impl_->whiteCube) atIndex:14];
        [impl_->currentEncoder setFragmentTexture:findTex(hasPrecomputed?surface.light.brdfLut:0,impl_->whiteTex) atIndex:15];
    }
    const unsigned int resolvedTextureId = textureId ? textureId : impl_->whiteTex;
    const auto samplerIt = impl_->textureSamplers.find(resolvedTextureId);
    [impl_->currentEncoder setFragmentSamplerState:
        samplerIt != impl_->textureSamplers.end() ? samplerIt->second : impl_->sampler
                                              atIndex:0];

    MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
    if (mode == DrawMode::Lines) prim = MTLPrimitiveTypeLine;
    else if (mode == DrawMode::Points) prim = MTLPrimitiveTypePoint;

    [impl_->currentEncoder drawPrimitives:prim vertexStart:0 vertexCount:count instanceCount:impl_->instances.empty()?1:impl_->instances.size()];
}

bool MetalBackend::SupportsGpuSkinning() const { return impl_->activeShader==0; }
void MetalBackend::SetSurface(const Surface& surface) { impl_->surface=surface; }

// ---------------------------------------------------------------------------
// Directional shadow mapping
// ---------------------------------------------------------------------------
void MetalBackend::BeginShadowPass(const Matrix& lightViewProj, int resolution) {
    resolution = resolution > 0 ? resolution : 1024;
    if (impl_->shadowTexture == nil || resolution != impl_->shadowResolution) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                         width:resolution height:resolution mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        impl_->shadowTexture = [impl_->device newTextureWithDescriptor:td];
        impl_->shadowResolution = resolution;
    }
    // Close any active screen/framebuffer encoder before the depth pass.
    if (impl_->currentEncoder) { [impl_->currentEncoder endEncoding]; impl_->currentEncoder = nil; }
    if (!impl_->currentCmd) impl_->currentCmd = [impl_->queue commandBuffer];

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.depthAttachment.texture = impl_->shadowTexture;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;
    impl_->currentEncoder = [impl_->currentCmd renderCommandEncoderWithDescriptor:rp];
    [impl_->currentEncoder setRenderPipelineState:impl_->shadowPipeline];
    [impl_->currentEncoder setDepthStencilState:impl_->depthStateOn];
    [impl_->currentEncoder setViewport:MTLViewport{0,0,(double)resolution,(double)resolution,0,1}];

    impl_->shadowPass = true;
    impl_->shadowMatrix = lightViewProj;
    // Render occluders with the light's view-projection; identity modelview.
    impl_->projection = lightViewProj;
    impl_->modelview = MatrixIdentity();
}

void MetalBackend::EndShadowPass() {
    if (!impl_->shadowPass) return;
    if (impl_->currentEncoder) { [impl_->currentEncoder endEncoding]; impl_->currentEncoder = nil; }
    impl_->shadowPass = false;
    impl_->hasShadowMap = true;   // shadowTexture is now sampleable by lit draws
}

void MetalBackend::ClearShadowMap() { impl_->hasShadowMap = false; impl_->cascadeCount = 0; }

// ---------------------------------------------------------------------------
// Cascaded shadow maps: N depth textures, one per view-frustum slice.
// ---------------------------------------------------------------------------
void MetalBackend::BeginShadowCascades(int count, int resolution) {
    count = std::clamp(count, 1, Impl::kMaxCascades);
    resolution = resolution > 0 ? resolution : 2048;
    if (impl_->cascadeTexture[0] == nil || resolution != impl_->cascadeResolution) {
        for (int i = 0; i < Impl::kMaxCascades; ++i) impl_->cascadeTexture[i] = nil;
        for (int i = 0; i < count; ++i) {
            MTLTextureDescriptor* td = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:resolution height:resolution mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModePrivate;
            impl_->cascadeTexture[i] = [impl_->device newTextureWithDescriptor:td];
        }
        impl_->cascadeResolution = resolution;
    }
    impl_->cascadeCount = 0;             // finalized in EndShadowCascades
    impl_->activeCascade = -1;
    impl_->cascadeResolution = resolution;
    // Remember how many cascades this set records via cascadeSplit slot count.
    for (int i = 0; i < Impl::kMaxCascades; ++i) impl_->cascadeSplit[i] = 0;
    impl_->cascadeSplit[0] = static_cast<float>(count); // stash pending count
}

void MetalBackend::BeginShadowCascade(int index, const Matrix& lightViewProj) {
    if (index < 0 || index >= Impl::kMaxCascades || impl_->cascadeTexture[index] == nil) return;
    impl_->cascadeMatrix[index] = lightViewProj;
    impl_->activeCascade = index;
    if (impl_->currentEncoder) { [impl_->currentEncoder endEncoding]; impl_->currentEncoder = nil; }
    if (!impl_->currentCmd) impl_->currentCmd = [impl_->queue commandBuffer];
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.depthAttachment.texture = impl_->cascadeTexture[index];
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;
    impl_->currentEncoder = [impl_->currentCmd renderCommandEncoderWithDescriptor:rp];
    [impl_->currentEncoder setRenderPipelineState:impl_->shadowPipeline];
    [impl_->currentEncoder setDepthStencilState:impl_->depthStateOn];
    [impl_->currentEncoder setViewport:MTLViewport{0,0,(double)impl_->cascadeResolution,(double)impl_->cascadeResolution,0,1}];
    impl_->shadowPass = true;
    impl_->shadowMatrix = lightViewProj; // reuse the depth-pass push path
    impl_->projection = lightViewProj;
    impl_->modelview = MatrixIdentity();
}

void MetalBackend::EndShadowCascade() {
    if (!impl_->shadowPass) return;
    if (impl_->currentEncoder) { [impl_->currentEncoder endEncoding]; impl_->currentEncoder = nil; }
    impl_->shadowPass = false;
    impl_->activeCascade = -1;
}

void MetalBackend::EndShadowCascades(const float* splitDepths, int count, const Matrix& viewMatrix) {
    count = std::clamp(count, 1, Impl::kMaxCascades);
    for (int i = 0; i < count; ++i) impl_->cascadeSplit[i] = splitDepths[i];
    impl_->cascadeViewMatrix = viewMatrix;
    impl_->cascadeCount = count;         // enable cascaded sampling on lit draws
    impl_->hasShadowMap = false;         // single-map path off while cascades active
}
void MetalBackend::SetSkinning(const Matrix* bones,int count) {
    impl_->bones.resize(std::max(0,count));
    for(int i=0;i<count;++i) impl_->bones[i]=MatrixTranspose(bones[i]);
}
void MetalBackend::DrawVerticesInstanced(const Vertex* verts, std::size_t count,
                                        unsigned int textureId, const Matrix* transforms, int instances) {
    if(impl_->activeShader) { RenderBackend::DrawVerticesInstanced(verts,count,textureId,transforms,instances); return; }
    impl_->instances.resize(instances);
    for(int i=0;i<instances;++i) impl_->instances[i]=MatrixTranspose(transforms[i]);
    try { DrawVertices(verts,count,DrawMode::Triangles,textureId); }
    catch(...) { impl_->instances.clear(); throw; }
    impl_->instances.clear();
}

unsigned int MetalBackend::UploadMeshBuffer(const Vertex* verts, std::size_t count) {
    if(!verts || count==0) return 0;
    id<MTLBuffer> buffer=[impl_->device newBufferWithBytes:verts length:count*sizeof(Vertex)
                                                   options:MTLResourceStorageModeShared];
    if(!buffer) return 0;
    const unsigned int handle=impl_->nextMeshBuffer++;
    impl_->meshBuffers[handle]=buffer;
    return handle;
}

void MetalBackend::DestroyMeshBuffer(unsigned int handle) {
    impl_->meshBuffers.erase(handle);
}

bool MetalBackend::DrawMeshBuffer(unsigned int handle, std::size_t count,
                                  unsigned int textureId, const Matrix* transforms, int instances) {
    auto it=impl_->meshBuffers.find(handle);
    if(it==impl_->meshBuffers.end() || impl_->activeShader) return false; // fall back
    impl_->instances.resize(std::max(1,instances));
    for(int i=0;i<instances;++i) impl_->instances[i]=MatrixTranspose(transforms[i]);
    if(instances<=0) impl_->instances[0]=MatrixTranspose(MatrixIdentity());
    impl_->boundMeshBuffer=it->second;
    try { DrawVertices(nullptr,count,DrawMode::Triangles,textureId); }
    catch(...) { impl_->boundMeshBuffer=nil; impl_->instances.clear(); throw; }
    impl_->boundMeshBuffer=nil;
    impl_->instances.clear();
    return true;
}

static MTLPixelFormat CompressedMetalFormat(PixelFormat format) {
    switch(format) {
        case PixelFormat::Compressed_DXT1_RGB: case PixelFormat::Compressed_DXT1_RGBA: return MTLPixelFormatBC1_RGBA;
        case PixelFormat::Compressed_DXT3_RGBA: return MTLPixelFormatBC2_RGBA;
        case PixelFormat::Compressed_DXT5_RGBA: return MTLPixelFormatBC3_RGBA;
        case PixelFormat::Compressed_ETC1_RGB: case PixelFormat::Compressed_ETC2_RGB: return MTLPixelFormatETC2_RGB8;
        case PixelFormat::Compressed_ETC2_EAC_RGBA: return MTLPixelFormatEAC_RGBA8;
        case PixelFormat::Compressed_ASTC_4x4_RGBA: return MTLPixelFormatASTC_4x4_LDR;
        case PixelFormat::Compressed_ASTC_8x8_RGBA: return MTLPixelFormatASTC_8x8_LDR;
        case PixelFormat::Compressed_PVRT_RGB:return MTLPixelFormatPVRTC_RGB_4BPP;
        case PixelFormat::Compressed_PVRT_RGBA:return MTLPixelFormatPVRTC_RGBA_4BPP;
        default: return MTLPixelFormatInvalid;
    }
}
bool MetalBackend::SupportsTextureFormat(PixelFormat format) const {
    int value=static_cast<int>(format);
    if(value>=1 && value<=13) return true;
    if(format>=PixelFormat::Compressed_DXT1_RGB && format<=PixelFormat::Compressed_DXT5_RGBA) return impl_->device.supportsBCTextureCompression;
    return CompressedMetalFormat(format)!=MTLPixelFormatInvalid && [impl_->device supportsFamily:MTLGPUFamilyApple2];
}
unsigned int MetalBackend::CreateTexture(const void* pixels, int width,
                                         int height, PixelFormat format) {
    const auto compressed=CompressedMetalFormat(format);
    if(compressed!=MTLPixelFormatInvalid) {
        if(!pixels || width<=0 || height<=0 || !SupportsTextureFormat(format)) return 0;
        const bool pvrtc=format==PixelFormat::Compressed_PVRT_RGB||format==PixelFormat::Compressed_PVRT_RGBA;
        if(pvrtc&&(width<8||height<8||(width&(width-1))||(height&(height-1))))return 0;
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:compressed width:width height:height mipmapped:NO];
        if(format==PixelFormat::Compressed_DXT1_RGB) descriptor.swizzle=MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed,MTLTextureSwizzleGreen,MTLTextureSwizzleBlue,MTLTextureSwizzleOne);
        descriptor.usage=MTLTextureUsageShaderRead;
        auto texture=[impl_->device newTextureWithDescriptor:descriptor];
        if(!texture) return 0;
        [texture replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0 withBytes:pixels bytesPerRow:pvrtc?0:CompressedSize(width,1,format)];
        unsigned int id=impl_->nextTextureId++; impl_->textures[id]=texture;
        impl_->textureWraps[id]=0; SetTextureFilter(id,1);
        return id;
    }
    auto upload=ConvertUpload(pixels,width,height,format);
    if(!upload.valid) return 0;
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:upload.floating?MTLPixelFormatRGBA32Float:MTLPixelFormatRGBA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:YES];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [impl_->device newTextureWithDescriptor:td];
    if(!tex) return 0;
    {
        MTLRegion region = {{0, 0, 0}, {static_cast<NSUInteger>(width),
                                        static_cast<NSUInteger>(height), 1}};
        [tex replaceRegion:region mipmapLevel:0 withBytes:upload.data()
               bytesPerRow:upload.stride(width)];
    }
    const unsigned int id = impl_->nextTextureId++;
    impl_->textures[id] = tex;
    impl_->textureFilters[id] = 1;
    impl_->textureWraps[id] = 0;
    SetTextureFilter(id,1);
    return id;
}

void MetalBackend::DestroyTexture(unsigned int textureId) {
    impl_->textures.erase(textureId);
    impl_->textureSamplers.erase(textureId);
    impl_->textureFilters.erase(textureId);
    impl_->textureWraps.erase(textureId);
}

void MetalBackend::UpdateTexture(unsigned int textureId, int width, int height,
                                 PixelFormat format, const void* pixels) {
    auto it = impl_->textures.find(textureId);
    if (it == impl_->textures.end() || pixels == nullptr) return;
    if (width <= 0 || height <= 0 || width != it->second.width || height != it->second.height) return;
    auto upload=ConvertUpload(pixels,width,height,format);
    const auto compressed=CompressedMetalFormat(format);
    if(it->second.storageMode==MTLStorageModePrivate) return;
    if(compressed==MTLPixelFormatInvalid) {
        if(!upload.valid || it->second.pixelFormat!=(upload.floating?MTLPixelFormatRGBA32Float:MTLPixelFormatRGBA8Unorm)) return;
    } else if(it->second.pixelFormat!=compressed) return;
    // Shared texture writes must not race draws already encoded this frame.
    if (impl_->currentCmd) {
        EndEncoding(impl_);
        [impl_->currentCmd commit];
        [impl_->currentCmd waitUntilCompleted];
        impl_->currentCmd = [impl_->queue commandBuffer];
    }
    MTLRegion region = {{0, 0, 0}, {static_cast<NSUInteger>(width),
                                    static_cast<NSUInteger>(height), 1}};
    [it->second replaceRegion:region mipmapLevel:0 withBytes:compressed==MTLPixelFormatInvalid?upload.data():pixels
                  bytesPerRow:compressed==MTLPixelFormatInvalid?upload.stride(width):(format==PixelFormat::Compressed_PVRT_RGB||format==PixelFormat::Compressed_PVRT_RGBA)?0:CompressedSize(width,1,format)];
}

int MetalBackend::GenTextureMipmaps(unsigned int textureId) {
    const auto it = impl_->textures.find(textureId);
    if (it == impl_->textures.end()) return 0;
    if (it->second.mipmapLevelCount <= 1) return 1;

    // A blit encoder cannot overlap a render encoder. Ending here is safe: the
    // next draw lazily begins a load pass on the same render target.
    EndEncoding(impl_);
    const bool ownsCommandBuffer = impl_->currentCmd == nil;
    id<MTLCommandBuffer> cmd = ownsCommandBuffer
        ? [impl_->queue commandBuffer]
        : impl_->currentCmd;
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit generateMipmapsForTexture:it->second];
    [blit endEncoding];
    if (ownsCommandBuffer) {
        [cmd commit];
        [cmd waitUntilCompleted];
    }
    return static_cast<int>(it->second.mipmapLevelCount);
}

void MetalBackend::SetTextureFilter(unsigned int textureId, int filter) {
    if (!impl_->textures.contains(textureId)) return;
    impl_->textureFilters[textureId] = filter;

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = filter == 0 ? MTLSamplerMinMagFilterNearest
                               : MTLSamplerMinMagFilterLinear;
    sd.magFilter = filter == 0 ? MTLSamplerMinMagFilterNearest
                               : MTLSamplerMinMagFilterLinear;
    sd.mipFilter = filter >= 2 ? MTLSamplerMipFilterLinear
                               : MTLSamplerMipFilterNotMipmapped;
    if (filter >= 3) {
        const NSUInteger anisotropy[] = {4, 8, 16};
        sd.maxAnisotropy = anisotropy[filter > 5 ? 2 : filter - 3];
    }
    const int wrap = impl_->textureWraps[textureId];
    const MTLSamplerAddressMode addressModes[] = {
        MTLSamplerAddressModeRepeat,
        MTLSamplerAddressModeClampToEdge,
        MTLSamplerAddressModeMirrorRepeat,
        MTLSamplerAddressModeMirrorClampToEdge
    };
    sd.sAddressMode = addressModes[wrap >= 0 && wrap <= 3 ? wrap : 0];
    sd.tAddressMode = sd.sAddressMode;
    impl_->textureSamplers[textureId] =
        [impl_->device newSamplerStateWithDescriptor:sd];
}

void MetalBackend::SetTextureWrap(unsigned int textureId, int wrap) {
    if (!impl_->textures.contains(textureId)) return;
    impl_->textureWraps[textureId] = wrap;
    SetTextureFilter(textureId, impl_->textureFilters[textureId]);
}

unsigned int MetalBackend::WhiteTexture() const { return impl_->whiteTex; }

namespace {
template <typename T> Image ReadMetalTexture(T* impl, id<MTLTexture> texture,int slice=0) {
    if (!texture) return {};
    const auto format = texture.pixelFormat;
    const bool floating=format==MTLPixelFormatRGBA32Float;
    if (!floating && format != MTLPixelFormatRGBA8Unorm && format != MTLPixelFormatBGRA8Unorm) return {};
    EndEncoding(impl);
    const NSUInteger width = texture.width, height = texture.height;
    const NSUInteger stride = (width * (floating?16:4) + 255) & ~NSUInteger(255);
    id<MTLBuffer> buffer = [impl->device newBufferWithLength:stride * height options:MTLResourceStorageModeShared];
    if (!buffer) return {};
    bool inFrame = impl->currentCmd != nil;
    id<MTLCommandBuffer> cmd = inFrame ? impl->currentCmd : [impl->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:slice sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
               sourceSize:MTLSizeMake(width,height,1) toBuffer:buffer destinationOffset:0
               destinationBytesPerRow:stride destinationBytesPerImage:stride * height];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    if (inFrame) impl->currentCmd = [impl->queue commandBuffer];
    if (cmd.status == MTLCommandBufferStatusError) return {};
    Image result{};
    result.width = static_cast<int>(width); result.height = static_cast<int>(height);
    result.data = std::malloc(width * height * 4);
    if (!result.data) return {};
    auto* dst = static_cast<unsigned char*>(result.data);
    auto* src = static_cast<const unsigned char*>(buffer.contents);
    for (NSUInteger y = 0; y < height; ++y) {
        if(floating) {
            for(NSUInteger x=0;x<width*4;++x) {
                float value; std::memcpy(&value,src+y*stride+x*4,4);
                dst[y*width*4+x]=static_cast<unsigned char>(std::isnan(value)?0:std::clamp(value,0.0f,1.0f)*255.0f+0.5f);
            }
        } else std::memcpy(dst + y * width * 4, src + y * stride, width * 4);
        if (format == MTLPixelFormatBGRA8Unorm)
            for (NSUInteger x = 0; x < width; ++x) std::swap(dst[(y*width+x)*4], dst[(y*width+x)*4+2]);
    }
    return result;
}
}

Image MetalBackend::ReadTexture(unsigned int textureId) {
    auto it = impl_->textures.find(textureId);
    if(it==impl_->textures.end()) return {};
    auto texture=it->second;
    if(texture.pixelFormat==MTLPixelFormatRGBA8Unorm || texture.pixelFormat==MTLPixelFormatBGRA8Unorm || texture.pixelFormat==MTLPixelFormatRGBA32Float)
        return ReadMetalTexture(impl_,texture);
    // Decode compressed texels on the GPU before copying to CPU RGBA8 storage.
    auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:texture.width height:texture.height mipmapped:NO];
    descriptor.usage=MTLTextureUsageShaderWrite|MTLTextureUsageShaderRead;
    auto decoded=[impl_->device newTextureWithDescriptor:descriptor];
    NSError* error=nil;
    auto pipeline=[impl_->device newComputePipelineStateWithFunction:[impl_->library newFunctionWithName:@"decode_texture"] error:&error];
    if(!decoded || !pipeline) return {};
    EndEncoding(impl_);
    bool ownsCommand=impl_->currentCmd==nil;
    if(ownsCommand) impl_->currentCmd=[impl_->queue commandBuffer];
    auto encoder=[impl_->currentCmd computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setTexture:texture atIndex:0]; [encoder setTexture:decoded atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(texture.width,texture.height,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    [encoder endEncoding];
    Image result=ReadMetalTexture(impl_,decoded);
    if(ownsCommand) impl_->currentCmd=nil;
    return result;
}
Image MetalBackend::ReadScreen() {
    return ReadMetalTexture(impl_, impl_->currentDrawable.texture);
}
unsigned int MetalBackend::CreateCubemap(const void* pixels,int size) {
    if(!pixels||size<=0||size>16384)return 0;
    auto descriptor=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:size mipmapped:YES];
    descriptor.usage=MTLTextureUsageShaderRead;
    auto texture=[impl_->device newTextureWithDescriptor:descriptor];if(!texture)return 0;
    const size_t bytes=static_cast<size_t>(size)*size*4;
    for(int face=0;face<6;++face)[texture replaceRegion:MTLRegionMake2D(0,0,size,size) mipmapLevel:0 slice:face withBytes:static_cast<const unsigned char*>(pixels)+face*bytes bytesPerRow:size*4 bytesPerImage:bytes];
    unsigned id=impl_->nextTextureId++;impl_->textures[id]=texture;impl_->textureWraps[id]=1;SetTextureFilter(id,1);return id;
}
Image MetalBackend::ReadCubemapFace(unsigned int id,int face) {
    auto found=impl_->textures.find(id);if(found==impl_->textures.end()||found->second.textureType!=MTLTextureTypeCube||face<0||face>=6)return {};
    return ReadMetalTexture(impl_,found->second,face);
}

// Framebuffers are represented by a color texture that is also registered in
// the normal texture table, plus a private depth texture.
unsigned int MetalBackend::CreateFramebuffer(int width, int height,
                                              unsigned int* colorTexOut) {
    if (colorTexOut) *colorTexOut = 0;
    if (width <= 0 || height <= 0 || !impl_->device) return 0;

    MTLTextureDescriptor* colorDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:static_cast<NSUInteger>(width)
                                    height:static_cast<NSUInteger>(height)
                                 mipmapped:NO];
    colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    colorDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> color = [impl_->device newTextureWithDescriptor:colorDesc];

    MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:static_cast<NSUInteger>(width)
                                    height:static_cast<NSUInteger>(height)
                                 mipmapped:NO];
    depthDesc.usage = MTLTextureUsageRenderTarget;
    depthDesc.sampleCount=impl_->samples;
    if(impl_->samples>1) depthDesc.textureType=MTLTextureType2DMultisample;
    depthDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [impl_->device newTextureWithDescriptor:depthDesc];
    if (!color || !depth) return 0;

    const unsigned int textureId = impl_->nextTextureId++;
    const unsigned int framebufferId = impl_->nextFramebufferId++;
    impl_->textures[textureId] = color;
    impl_->textureFilters[textureId] = 1;
    impl_->textureWraps[textureId] = 1;
    impl_->framebuffers.emplace(framebufferId, Impl::Framebuffer{
        color, depth, width, height, textureId
    });
    impl_->framebuffers.at(framebufferId).multisample=MultisampleColor(impl_->device,width,height,impl_->samples);
    if (colorTexOut) *colorTexOut = textureId;
    return framebufferId;
}

void MetalBackend::DestroyFramebuffer(unsigned int fboId,
                                      unsigned int colorTexId) {
    if (impl_->activeFramebuffer == fboId) BindFramebuffer(0, impl_->width, impl_->height);
    impl_->framebuffers.erase(fboId);
    impl_->textures.erase(colorTexId);
    impl_->textureSamplers.erase(colorTexId);
    impl_->textureFilters.erase(colorTexId);
    impl_->textureWraps.erase(colorTexId);
}

void MetalBackend::BindFramebuffer(unsigned int fboId, int width, int height) {
    EndEncoding(impl_);
    if (fboId != 0 && !impl_->framebuffers.contains(fboId)) {
        std::fprintf(stderr, "[meowyrender][Metal] invalid framebuffer %u\n", fboId);
        return;
    }
    impl_->activeFramebuffer = fboId;
    // Screen drawable size is physical pixels, updated only by Resize.
}
unsigned int MetalBackend::CreateShaderProgram(const char* vsSrc,
                                               const char* fsSrc) {
    if (!vsSrc || !fsSrc) return 0;
    const std::string combined = std::string(vsSrc) + "\n" + fsSrc;
    NSString* source = [NSString stringWithUTF8String:combined.c_str()];
    NSError* error = nil;
    id<MTLLibrary> library = [impl_->device newLibraryWithSource:source
                                                        options:nil
                                                          error:&error];
    if (!library) {
        std::fprintf(stderr, "[meowyrender][Metal] custom MSL compile failed: %s\n",
                     error ? error.localizedDescription.UTF8String : "unknown");
        return 0;
    }

    id<MTLFunction> vertex = [library newFunctionWithName:@"vs_main"];
    id<MTLFunction> fragment = [library newFunctionWithName:@"fs_main"];
    if (!vertex || !fragment) {
        std::fprintf(stderr,
            "[meowyrender][Metal] custom MSL must define vs_main and fs_main\n");
        return 0;
    }

    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat3;
    vd.attributes[0].offset = offsetof(Vertex, x);
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = offsetof(Vertex, u);
    vd.attributes[1].bufferIndex = 0;
    vd.attributes[2].format = MTLVertexFormatUChar4Normalized;
    vd.attributes[2].offset = offsetof(Vertex, r);
    vd.attributes[2].bufferIndex = 0;
    vd.layouts[0].stride = sizeof(Vertex);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = vertex;
    pd.fragmentFunction = fragment;
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    pd.rasterSampleCount=impl_->samples;

    MTLRenderPipelineReflection* reflection = nil;
    id<MTLRenderPipelineState> pipeline =
        [impl_->device newRenderPipelineStateWithDescriptor:pd
                                                    options:MTLPipelineOptionBindingInfo |
                                                            MTLPipelineOptionBufferTypeInfo
                                                 reflection:&reflection
                                                      error:&error];
    if (!pipeline) {
        std::fprintf(stderr, "[meowyrender][Metal] custom pipeline failed: %s\n",
                     error ? error.localizedDescription.UTF8String : "unknown");
        return 0;
    }

    Impl::ShaderProgram program;
    program.library = library;
    program.pipeline = pipeline;
    program.descriptor = pd;
    for(NSArray<id<MTLBinding>>* bindings in @[reflection.vertexBindings,reflection.fragmentBindings])
    for (id<MTLBinding> binding in bindings) {
        if (binding.type != MTLBindingTypeBuffer || binding.index != 2) continue;
        id<MTLBufferBinding> buffer = static_cast<id<MTLBufferBinding>>(binding);
        if (buffer.bufferStructType == nil) continue;
        program.uniformData.resize(std::max<NSUInteger>(program.uniformData.size(),buffer.bufferDataSize), 0);
        for (MTLStructMember* member in buffer.bufferStructType.members) {
            const auto dataType=member.dataType==MTLDataTypeArray?member.arrayType.elementType:member.dataType;
            const NSUInteger size=MetalDataTypeSize(dataType),count=member.dataType==MTLDataTypeArray?member.arrayType.arrayLength:1;
            const NSUInteger stride=member.dataType==MTLDataTypeArray?member.arrayType.stride:size;
            int type=-1;
            switch(dataType){case MTLDataTypeFloat:type=0;break;case MTLDataTypeFloat2:type=1;break;case MTLDataTypeFloat3:type=2;break;case MTLDataTypeFloat4:type=3;break;case MTLDataTypeInt:type=4;break;case MTLDataTypeFloat4x4:type=5;break;default:break;}
            if(!size||type<0)continue;
            auto duplicate=std::find_if(program.uniforms.begin(),program.uniforms.end(),[&](const auto& value){return value.name==member.name.UTF8String;});
            if(duplicate!=program.uniforms.end()) {
                if(duplicate->offset!=member.offset||duplicate->type!=type||duplicate->stride!=stride||duplicate->count!=count){std::fprintf(stderr,"[meowyrender][Metal] vertex and fragment uniform layouts must match\n");return 0;}
            }else program.uniforms.push_back({member.name.UTF8String,member.offset,size,count,stride,type});
        }
    }

    const unsigned int id = impl_->nextShaderId++;
    impl_->shaders.emplace(id, std::move(program));
    return id;
}

void MetalBackend::DestroyShaderProgram(unsigned int programId) {
    if (impl_->activeShader == programId) impl_->activeShader = 0;
    impl_->shaders.erase(programId);
}

int MetalBackend::GetShaderUniformLocation(unsigned int programId,
                                           const char* name) {
    const auto it = impl_->shaders.find(programId);
    if (it == impl_->shaders.end() || !name) return -1;
    for (std::size_t i = 0; i < it->second.uniforms.size(); ++i) {
        if (it->second.uniforms[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void MetalBackend::SetShaderUniform(unsigned int programId, int location,
                                    const void* value, int uniformType,
                                    int count) {
    auto it = impl_->shaders.find(programId);
    if (it == impl_->shaders.end() || !value || location < 0 ||
        static_cast<std::size_t>(location) >= it->second.uniforms.size()) return;
    auto& binding = it->second.uniforms[location];
    const NSUInteger componentCounts[] = {1, 2, 3, 4, 1,16};
    if (uniformType < 0 || uniformType > 5 || count <= 0) return;
    if(uniformType!=binding.type||static_cast<NSUInteger>(count)>binding.count)throw std::invalid_argument("Uniform type or array count mismatch");
    const NSUInteger elementSize = sizeof(std::uint32_t) * componentCounts[uniformType];
    if(binding.offset+(count-1)*binding.stride+elementSize>it->second.uniformData.size())throw std::runtime_error("Uniform outside reflected buffer");
    for(int i=0;i<count;++i) {
        void* output=it->second.uniformData.data()+binding.offset+i*binding.stride;
        if(uniformType==5){Matrix matrix;std::memcpy(&matrix,static_cast<const char*>(value)+i*elementSize,sizeof(matrix));matrix=MatrixTranspose(matrix);std::memcpy(output,&matrix,sizeof(matrix));}
        else std::memcpy(output,static_cast<const char*>(value)+i*elementSize,elementSize);
    }
}

void MetalBackend::SetActiveShader(unsigned int programId) {
    impl_->activeShader = impl_->shaders.contains(programId) ? programId : 0;
}

#if defined(MEOWY_WITH_IMGUI)
// Dear ImGui on Metal: GLFW platform backend (for input) + the Metal renderer
// backend. imgui_impl_glfw uses ImGui_ImplGlfw_InitForOther on non-GL contexts.
bool MetalBackend::ImGuiInit(void* windowHandle) {
    if (!impl_->device) return false;
#if defined(MEOWY_PLATFORM_VISIONOS)
    // "By Metaling the way": on visionOS there is no GLFW platform backend, so
    // we use only imgui_impl_metal for rendering and feed ImGui IO ourselves
    // from the UIView (display size/scale) and the engine input state (mouse
    // from touches, keys) in ImGuiNewFrame.
    if (!impl_->view) return false;
    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "meowyrender_visionos";
    if (!ImGui_ImplMetal_Init(impl_->device)) {
        ImGui::DestroyContext();
        return false;
    }
    return true;
#else
    auto* window = static_cast<GLFWwindow*>(windowHandle);
    if (!window) return false;
    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }
    if (!ImGui_ImplGlfw_InitForOther(window, true)) { ImGui::DestroyContext(); return false; }
    if (!ImGui_ImplMetal_Init(impl_->device)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    return true;
#endif
}
void MetalBackend::ImGuiNewFrame() {
    // ImGui_ImplMetal_NewFrame wants a render pass descriptor describing the
    // current target (it reads the framebuffer size/pixel format). Build one for
    // the current drawable; the actual draw happens in ImGuiRender.
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    id<MTLTexture> color = impl_->currentDrawable ? impl_->currentDrawable.texture : nil;
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    ImGui_ImplMetal_NewFrame(rp);
#if defined(MEOWY_PLATFORM_VISIONOS)
    // Custom (GLFW-free) platform layer: feed display size/scale from the UIView
    // and pointer/keyboard state from the engine's input (fed by the UIKit view
    // in platform_visionos.mm). This is the minimal platform plumbing ImGui
    // needs to lay out and interact with widgets.
    ImGuiIO& io = ImGui::GetIO();
    const CGFloat scale = std::max<CGFloat>(1, impl_->view.contentScaleFactor);
    const CGSize pts = impl_->view.bounds.size;
    io.DisplaySize = ImVec2(static_cast<float>(pts.width), static_cast<float>(pts.height));
    io.DisplayFramebufferScale = ImVec2(static_cast<float>(scale), static_cast<float>(scale));
    const double frameTime = detail::State().timing.frameTime;
    io.DeltaTime = frameTime > 0.0 ? static_cast<float>(frameTime) : 1.0f / 60.0f;
    // Pointer: the platform reports touch position in points (view space), which
    // matches ImGui's DisplaySize coordinate space. Button 0 = active touch.
    const auto& input = detail::State().input;
    io.AddMousePosEvent(input.mousePosition.x, input.mousePosition.y);
    io.AddMouseButtonEvent(0, input.mouseCurrent[0]);
    ImGui::NewFrame();
#else
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
#endif
}
void MetalBackend::ImGuiRender() {
    ImGui::Render();
    // Ensure a render encoder targeting the drawable exists (load, so the scene
    // that was already drawn is preserved), then render ImGui into it. Shared by
    // desktop Metal and visionOS (both render via imgui_impl_metal).
    BeginEncoding(impl_, MTLLoadActionLoad, impl_->clearColor);
    if (!impl_->currentEncoder || !impl_->currentCmd) return;
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), impl_->currentCmd, impl_->currentEncoder);
}
void MetalBackend::ImGuiShutdown() {
    ImGui_ImplMetal_Shutdown();
#if !defined(MEOWY_PLATFORM_VISIONOS)
    ImGui_ImplGlfw_Shutdown();
#endif
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
}
#endif // MEOWY_WITH_IMGUI

} // namespace meowyrender::backend::metal
