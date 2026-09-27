// meowyrender - src/backend/vulkan/vk_backend.cpp  (internal)
//
// Vulkan backend. Creates a VkInstance + surface (via GLFW), picks a physical
// device, and sets up the queue/swapchain scaffolding. On Apple platforms the
// portability enumeration extension is enabled so MoltenVK is used as the
// Vulkan implementation over Metal.
//
// Swapchain and offscreen passes, streaming geometry, runtime GLSL, skinning,
// materials and texture readback use a shared descriptor/pipeline interface.
#include "backend/vulkan/vk_backend.hpp"
#include "core/mr_state.hpp"

#if defined(__APPLE__)
#  define VK_USE_PLATFORM_MACOS_MVK
#endif
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <stdexcept>
#include <cstdlib>
#include "batch_vert.h"
#include "batch_frag.h"
#include "decode_comp.h"
#include "shadow_vert.h"
#include "shadow_frag.h"
#include "backend/pixel_conversion.hpp"
#include "runtime_shader.hpp"

#if defined(MEOWY_WITH_IMGUI)
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#endif

namespace meowyrender::backend::vulkan {

struct VulkanBackend::Impl {
    struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; };
    struct Attachment {VkImage image{};VkDeviceMemory memory{};VkImageView view{};};
    Attachment multisampleColor;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    struct Texture { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; VkSampler sampler{}; VkDescriptorSet descriptor{}; VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;int width{}, height{},layers=1; int filter=1, wrap=0; uint32_t levels=1; bool mipmaps=false; };
    std::unordered_map<unsigned int, Texture> textures;
    struct Target {unsigned int color=0; VkImage depth{}; VkDeviceMemory memory{}; VkImageView view{},colorView{}; VkFramebuffer framebuffer{}; VkExtent2D extent{}; bool initialized=false;Attachment multisample;};
    std::unordered_map<unsigned int,Target> targets;
    unsigned int activeTarget=0,nextTarget=1;
    VkRenderPass targetPass{},targetLoadPass{};
    VkPipeline targetPipelines[9]{},targetDepthPipelines[9]{},targetDepthNoWritePipelines[9]{};
    struct Program {RuntimeShader shader; VkPipeline screen[9]{},screenDepth[9]{},screenDepthNoWrite[9]{},target[9]{},targetDepth[9]{},targetDepthNoWrite[9]{};};
    std::unordered_map<unsigned int,Program> programs;
    unsigned int activeProgram=0,nextProgram=1;
    std::vector<VkDescriptorSet> frameDescriptors;
    std::vector<Buffer> frameBuffers;
    std::vector<Matrix> instances,bones;
    Surface materialSurface;
    Rectangle viewport{0,0,1,1};
    VkDescriptorSetLayout descriptorLayout{};
    VkDescriptorPool descriptorPool{};
    VkPipelineLayout pipelineLayout{};
    VkDescriptorSetLayout decodeLayout{};VkPipelineLayout decodePipelineLayout{};VkPipeline decodePipeline{};
    bool pvrtc=false;
    VkPipeline pipelines[9]{};
    VkPipeline depthPipelines[9]{};
    VkPipeline depthNoWritePipelines[9]{};      // depth test, no write (transparent)
    VkImage depthImage{}; VkDeviceMemory depthMemory{}; VkImageView depthView{};
    bool depthEnabled=false;
    bool depthMask=true;
    int blendMode=0;
    bool scissorEnabled=false;
    int scissorX=0,scissorY=0,scissorW=0,scissorH=0;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass loadRenderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence frameFence = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    Color clearColor{0, 0, 0, 255};
    bool frameActive = false;
    bool passActive = false;
    bool acquireConsumed = false;
    bool captured = false;

    GLFWwindow* window = nullptr;
    int width = 0;
    int height = 0;

    Matrix projection;
    Matrix modelview;

    unsigned int whiteTex = 1;
    unsigned int whiteCube = 0;  // 1x1 white cubemap: IBL-disabled fallback for binding 8
    unsigned int nextTextureId = 2;

    // Persistent GPU mesh cache: device-resident vertex buffers keyed by handle.
    // When boundMeshBufferValid is set, DrawVertices binds boundMeshBuffer
    // instead of streaming a transient vertex buffer from CPU data.
    struct MeshBuffer { VkBuffer buffer{}; VkDeviceMemory memory{}; };
    std::unordered_map<unsigned int, MeshBuffer> meshBuffers;
    unsigned int nextMeshBuffer = 1;
    VkBuffer boundMeshBuffer = VK_NULL_HANDLE;
    bool boundMeshBufferValid = false;

    // Directional shadow mapping: a depth-only pass renders occluders into
    // shadowImage from the light's POV; the lit shader samples it at binding 9.
    VkImage shadowImage{}; VkDeviceMemory shadowMemory{}; VkImageView shadowView{}; VkSampler shadowSampler{};
    VkImage shadowDepthImage{}; VkDeviceMemory shadowDepthMemory{}; VkImageView shadowDepthView{};
    VkRenderPass shadowPass{}; VkFramebuffer shadowFramebuffer{}; VkPipeline shadowPipeline{};
    int shadowResolution=0;
    bool shadowActive=false;    // currently recording the depth pass
    bool hasShadowMap=false;    // shadowImage holds a valid map for lit draws
    Matrix shadowMatrix{};      // light view-projection

    bool usingMoltenVK = false;
    char name[64] = "Vulkan";

#if defined(MEOWY_WITH_IMGUI)
    // Dear ImGui: a dedicated descriptor pool (kept separate from the batch
    // renderer's pool so ImGui's font/image descriptors never compete with the
    // per-frame draw descriptors) and a ready flag gating the frame hooks.
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool imguiReady = false;
#endif
};

namespace {
VkFormat CompressedFormat(PixelFormat format) {
    switch(format) {
        case PixelFormat::Compressed_DXT1_RGB:return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case PixelFormat::Compressed_DXT1_RGBA:return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PixelFormat::Compressed_DXT3_RGBA:return VK_FORMAT_BC2_UNORM_BLOCK;
        case PixelFormat::Compressed_DXT5_RGBA:return VK_FORMAT_BC3_UNORM_BLOCK;
        case PixelFormat::Compressed_ETC1_RGB:case PixelFormat::Compressed_ETC2_RGB:return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case PixelFormat::Compressed_ETC2_EAC_RGBA:return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case PixelFormat::Compressed_ASTC_4x4_RGBA:return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case PixelFormat::Compressed_ASTC_8x8_RGBA:return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case PixelFormat::Compressed_PVRT_RGB:case PixelFormat::Compressed_PVRT_RGBA:return VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG;
        default:return VK_FORMAT_UNDEFINED;
    }
}
void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}
template <typename T> uint32_t MemoryType(T* s, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(s->physicalDevice, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("No compatible Vulkan memory type");
}
// Submit recorded work before changing resources referenced by it. The next
// render pass uses LOAD, preserving the frame across uploads and readback.
template <typename T> void SubmitPending(T* s) {
    if (!s->frameActive) { Check(vkDeviceWaitIdle(s->device), "wait resources"); return; }
    if (s->passActive) { vkCmdEndRenderPass(s->commandBuffer); s->passActive=false; }
    Check(vkEndCommandBuffer(s->commandBuffer), "end pending commands");
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1; submit.pCommandBuffers=&s->commandBuffer;
    submit.waitSemaphoreCount=s->acquireConsumed?0:1;
    submit.pWaitSemaphores=&s->imageAvailable; submit.pWaitDstStageMask=&stage;
    Check(vkQueueSubmit(s->graphicsQueue,1,&submit,VK_NULL_HANDLE), "submit pending commands");
    Check(vkQueueWaitIdle(s->graphicsQueue), "wait pending commands");
    s->acquireConsumed=true;
    Check(vkResetCommandBuffer(s->commandBuffer,0), "reset pending commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(vkBeginCommandBuffer(s->commandBuffer,&begin), "resume pending commands");
}
template <typename T> VkExtent2D TargetExtent(T* s) {return s->activeTarget?s->targets.at(s->activeTarget).extent:s->swapchainExtent;}
template <typename T> void BeginScreenPass(T* s) {
    if (!s->frameActive || s->passActive) return;
    VkClearValue clears[2]{}; clears[0].color={{s->clearColor.r/255.f,s->clearColor.g/255.f,s->clearColor.b/255.f,s->clearColor.a/255.f}}; clears[1].depthStencil={1,0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass=s->captured?s->loadRenderPass:s->renderPass;
    begin.framebuffer=s->framebuffers[s->imageIndex]; begin.renderArea.extent=s->swapchainExtent;
    if(s->activeTarget) {
        auto& target=s->targets.at(s->activeTarget);
        begin.renderPass=target.initialized?s->targetLoadPass:s->targetPass;
        begin.framebuffer=target.framebuffer; begin.renderArea.extent=target.extent; target.initialized=true;
    } else s->captured=true;
    begin.clearValueCount=2; begin.pClearValues=clears;
    vkCmdBeginRenderPass(s->commandBuffer,&begin,VK_SUBPASS_CONTENTS_INLINE);
    s->passActive=true;
}
template <typename T, typename Texture> void ReplaceSampler(T* s, Texture& texture) {
    SubmitPending(s);
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter=info.minFilter=texture.filter==0?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
    info.mipmapMode=texture.filter>=2?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.maxLod=texture.mipmaps?static_cast<float>(texture.levels-1):0.0f;
    constexpr VkSamplerAddressMode wraps[]={VK_SAMPLER_ADDRESS_MODE_REPEAT,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    info.addressModeU=info.addressModeV=info.addressModeW=wraps[std::clamp(texture.wrap,0,3)];
    VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(s->physicalDevice,&features);
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(s->physicalDevice,&properties);
    if(texture.filter>=3 && features.samplerAnisotropy) {
        info.anisotropyEnable=VK_TRUE;
        info.maxAnisotropy=std::min(properties.limits.maxSamplerAnisotropy,static_cast<float>(1u<<std::clamp(texture.filter-1,2,4)));
    }
    VkSampler sampler{}; Check(vkCreateSampler(s->device,&info,nullptr,&sampler), "texture sampler");
    vkDestroySampler(s->device,texture.sampler,nullptr); texture.sampler=sampler;
}
template <typename T, typename B> void MakeBuffer(T* s, B& out, VkDeviceSize size, VkBufferUsageFlags usage, const void* data = nullptr) {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size = size; ci.usage = usage;
    Check(vkCreateBuffer(s->device, &ci, nullptr, &out.buffer), "create buffer");
    VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(s->device, out.buffer, &requirements);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = requirements.size;
    ai.memoryTypeIndex = MemoryType(s, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(s->device, &ai, nullptr, &out.memory), "allocate buffer");
    Check(vkBindBufferMemory(s->device, out.buffer, out.memory, 0), "bind buffer");
    if (data) { void* mapped{}; Check(vkMapMemory(s->device,out.memory,0,size,0,&mapped), "map buffer"); std::memcpy(mapped,data,size); vkUnmapMemory(s->device,out.memory); }
}
template <typename T, typename F> void Immediate(T* s, F encode) {
    VkCommandBuffer cmd{};
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool=s->commandPool; ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount=1;
    Check(vkAllocateCommandBuffers(s->device,&ai,&cmd), "allocate transfer command");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(cmd,&bi), "begin transfer"); encode(cmd); Check(vkEndCommandBuffer(cmd), "end transfer");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&cmd;
    Check(vkQueueSubmit(s->graphicsQueue,1,&si,VK_NULL_HANDLE), "submit transfer");
    Check(vkQueueWaitIdle(s->graphicsQueue), "wait transfer"); vkFreeCommandBuffers(s->device,s->commandPool,1,&cmd);
}
template <typename T> void CreatePipelines(T* s,bool target=false,typename T::Program* custom=nullptr) {
    VkShaderModule modules[2]{};
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize=custom?custom->shader.vertex.size()*4:sizeof(batch_vert); mi.pCode=custom?custom->shader.vertex.data():batch_vert; Check(vkCreateShaderModule(s->device,&mi,nullptr,&modules[0]), "vertex shader");
    mi.codeSize=custom?custom->shader.fragment.size()*4:sizeof(batch_frag); mi.pCode=custom?custom->shader.fragment.data():batch_frag; Check(vkCreateShaderModule(s->device,&mi,nullptr,&modules[1]), "fragment shader");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(int i=0;i<2;++i){ stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT; stages[i].module=modules[i]; stages[i].pName="main"; }
    VkVertexInputBindingDescription bindings[]={{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX},{1,sizeof(Matrix),VK_VERTEX_INPUT_RATE_INSTANCE}};
    VkVertexInputAttributeDescription attributes[]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,x)},{1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,u)},{2,0,VK_FORMAT_R8G8B8A8_UNORM,offsetof(Vertex,r)},
        {3,1,VK_FORMAT_R32G32B32A32_SFLOAT,0},{4,1,VK_FORMAT_R32G32B32A32_SFLOAT,16},{5,1,VK_FORMAT_R32G32B32A32_SFLOAT,32},{6,1,VK_FORMAT_R32G32B32A32_SFLOAT,48},{7,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,joints)},{8,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,weights)},{9,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,nx)}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vertex.vertexBindingDescriptionCount=2; vertex.pVertexBindingDescriptions=bindings; vertex.vertexAttributeDescriptionCount=custom?3:10; vertex.pVertexAttributeDescriptions=attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount=1; viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples=s->samples;
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask=15; blend.blendEnable=VK_TRUE; blend.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkPipelineColorBlendStateCreateInfo blends{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blends.attachmentCount=1; blends.pAttachments=&blend;
    VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamicStates;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vertex; pi.pInputAssemblyState=&assembly; pi.pViewportState=&viewport; pi.pRasterizationState=&raster; pi.pMultisampleState=&samples; pi.pColorBlendState=&blends; pi.pDynamicState=&dynamic; pi.layout=s->pipelineLayout; pi.renderPass=s->renderPass;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL; pi.pDepthStencilState=&depth;
    if(target) pi.renderPass=s->targetPass;
    auto* pipelines=custom?(target?custom->target:custom->screen):(target?s->targetPipelines:s->pipelines);
    auto* depthPipelines=custom?(target?custom->targetDepth:custom->screenDepth):(target?s->targetDepthPipelines:s->depthPipelines);
    auto* depthNoWritePipelines=custom?(target?custom->targetDepthNoWrite:custom->screenDepthNoWrite):(target?s->targetDepthNoWritePipelines:s->depthNoWritePipelines);
    const VkPrimitiveTopology topologies[]={VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,VK_PRIMITIVE_TOPOLOGY_LINE_LIST,VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
    for(int mode=0;mode<3;++mode) {
        blend.srcColorBlendFactor=mode==2?VK_BLEND_FACTOR_DST_COLOR:VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor=mode==2?VK_BLEND_FACTOR_ZERO:mode==1?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        // variant 0: no depth; 1: test+write; 2: test, no write (transparent).
        for(int variant=0;variant<3;++variant) {
            depth.depthTestEnable=variant?VK_TRUE:VK_FALSE;
            depth.depthWriteEnable=variant==1?VK_TRUE:VK_FALSE;
            auto* dst=variant==0?pipelines:variant==1?depthPipelines:depthNoWritePipelines;
            for(int i=0;i<3;++i){assembly.topology=topologies[i];Check(vkCreateGraphicsPipelines(s->device,VK_NULL_HANDLE,1,&pi,nullptr,&dst[mode*3+i]),"graphics pipeline");}
        }
    }
    for(auto module:modules) vkDestroyShaderModule(s->device,module,nullptr);
}
template<typename T,typename A> void DestroyAttachment(T* s,A& attachment) {
    if(attachment.view)vkDestroyImageView(s->device,attachment.view,nullptr);
    if(attachment.image)vkDestroyImage(s->device,attachment.image,nullptr);
    if(attachment.memory)vkFreeMemory(s->device,attachment.memory,nullptr);
    attachment={};
}
template<typename T,typename A> void CreateMultisampleColor(T* s,A& attachment,VkFormat format,VkExtent2D extent) {
    if(s->samples==VK_SAMPLE_COUNT_1_BIT)return;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={extent.width,extent.height,1};
    ci.mipLevels=ci.arrayLayers=1;ci.samples=s->samples;ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    Check(vkCreateImage(s->device,&ci,nullptr,&attachment.image),"multisample image");
    VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(s->device,attachment.image,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=MemoryType(s,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(s->device,&allocation,nullptr,&attachment.memory),"multisample memory");
    Check(vkBindImageMemory(s->device,attachment.image,attachment.memory,0),"multisample bind");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=attachment.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Check(vkCreateImageView(s->device,&view,nullptr,&attachment.view),"multisample view");
}
template<typename T> void CreateRenderPasses(T* s,VkFormat format,VkImageLayout finalLayout,VkRenderPass& clearPass,VkRenderPass& loadPass) {
    const bool multisample=s->samples!=VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentDescription attachments[3]{};
    attachments[0].format=format;attachments[0].samples=s->samples;
    attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachments[0].finalLayout=multisample?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:finalLayout;
    attachments[1].format=VK_FORMAT_D32_SFLOAT;attachments[1].samples=s->samples;
    attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[2].format=format;attachments[2].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[2].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachments[2].finalLayout=finalLayout;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},resolve{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;sub.pDepthStencilAttachment=&depth;
    if(multisample)sub.pResolveAttachments=&resolve;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;
    dependencies[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;dependencies[1].srcStageMask=dependencies[0].dstStageMask;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[1].srcAccessMask=dependencies[0].dstAccessMask;dependencies[1].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=multisample?3:2;pass.pAttachments=attachments;pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=2;pass.pDependencies=dependencies;
    Check(vkCreateRenderPass(s->device,&pass,nullptr,&clearPass),"clear render pass");
    attachments[0].loadOp=attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].initialLayout=attachments[0].finalLayout;attachments[1].initialLayout=attachments[1].finalLayout;
    Check(vkCreateRenderPass(s->device,&pass,nullptr,&loadPass),"load render pass");
}

template <typename T> void DestroySwapchain(T* s) {
    for(auto& [id,program]:s->programs) for(auto* array:{program.screen,program.screenDepth,program.screenDepthNoWrite})
        for(int i=0;i<9;++i) {if(array[i]) vkDestroyPipeline(s->device,array[i],nullptr); array[i]=VK_NULL_HANDLE;}
    for(auto& pipeline:s->depthPipelines){ if(pipeline) vkDestroyPipeline(s->device,pipeline,nullptr); pipeline=VK_NULL_HANDLE; }
    for(auto& pipeline:s->depthNoWritePipelines){ if(pipeline) vkDestroyPipeline(s->device,pipeline,nullptr); pipeline=VK_NULL_HANDLE; }
    for(auto& pipeline:s->pipelines){ if(pipeline) vkDestroyPipeline(s->device,pipeline,nullptr); pipeline=VK_NULL_HANDLE; }
    for (auto fb : s->framebuffers) vkDestroyFramebuffer(s->device, fb, nullptr);
    DestroyAttachment(s,s->multisampleColor);
    if(s->depthView) vkDestroyImageView(s->device,s->depthView,nullptr);
    if(s->depthImage) vkDestroyImage(s->device,s->depthImage,nullptr);
    if(s->depthMemory) vkFreeMemory(s->device,s->depthMemory,nullptr);
    s->depthView=VK_NULL_HANDLE; s->depthImage=VK_NULL_HANDLE; s->depthMemory=VK_NULL_HANDLE;
    for (auto view : s->swapchainViews) vkDestroyImageView(s->device, view, nullptr);
    s->framebuffers.clear(); s->swapchainViews.clear(); s->swapchainImages.clear();
    if (s->renderPass) vkDestroyRenderPass(s->device, s->renderPass, nullptr);
    if (s->loadRenderPass) vkDestroyRenderPass(s->device, s->loadRenderPass, nullptr);
    s->loadRenderPass=VK_NULL_HANDLE;
    if (s->swapchain) vkDestroySwapchainKHR(s->device, s->swapchain, nullptr);
    s->renderPass = VK_NULL_HANDLE; s->swapchain = VK_NULL_HANDLE;
}

template <typename T> bool CreateSwapchain(T* s) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s->physicalDevice, s->surface, &caps);
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(s->physicalDevice, s->surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(s->physicalDevice, s->surface, &count, formats.data());
    if (formats.empty()) return false;
    auto chosen = formats[0];
    for (const auto& f : formats) if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
    s->swapchainFormat = chosen.format;
    if (caps.currentExtent.width != UINT32_MAX) s->swapchainExtent = caps.currentExtent;
    else {
        int w = 0, h = 0; glfwGetFramebufferSize(s->window, &w, &h);
        s->swapchainExtent = {std::clamp<uint32_t>(w, caps.minImageExtent.width, caps.maxImageExtent.width),
                              std::clamp<uint32_t>(h, caps.minImageExtent.height, caps.maxImageExtent.height)};
    }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha))
        alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = s->surface; ci.minImageCount = imageCount; ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace; ci.imageExtent = s->swapchainExtent; ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform; ci.compositeAlpha = alpha;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; ci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(s->device, &ci, nullptr, &s->swapchain) != VK_SUCCESS) return false;
    vkGetSwapchainImagesKHR(s->device, s->swapchain, &imageCount, nullptr);
    s->swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(s->device, s->swapchain, &imageCount, s->swapchainImages.data());
    for (auto image : s->swapchainImages) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = chosen.format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        VkImageView view{}; if (vkCreateImageView(s->device, &vi, nullptr, &view) != VK_SUCCESS) return false;
        s->swapchainViews.push_back(view);
    }
    CreateRenderPasses(s,chosen.format,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,s->renderPass,s->loadRenderPass);
    CreateMultisampleColor(s,s->multisampleColor,chosen.format,s->swapchainExtent);
    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; depthInfo.imageType=VK_IMAGE_TYPE_2D; depthInfo.format=VK_FORMAT_D32_SFLOAT; depthInfo.extent={s->swapchainExtent.width,s->swapchainExtent.height,1}; depthInfo.mipLevels=depthInfo.arrayLayers=1; depthInfo.samples=s->samples; depthInfo.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    Check(vkCreateImage(s->device,&depthInfo,nullptr,&s->depthImage), "depth image");
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(s->device,s->depthImage,&requirements);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=requirements.size; ai.memoryTypeIndex=MemoryType(s,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(s->device,&ai,nullptr,&s->depthMemory), "depth memory"); Check(vkBindImageMemory(s->device,s->depthImage,s->depthMemory,0), "bind depth");
    VkImageViewCreateInfo dvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; dvi.image=s->depthImage; dvi.viewType=VK_IMAGE_VIEW_TYPE_2D; dvi.format=VK_FORMAT_D32_SFLOAT; dvi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}; Check(vkCreateImageView(s->device,&dvi,nullptr,&s->depthView), "depth view");
    for (auto view : s->swapchainViews) {
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        VkImageView views[]={s->samples==VK_SAMPLE_COUNT_1_BIT?view:s->multisampleColor.view,s->depthView,view};
        fi.renderPass = s->renderPass; fi.attachmentCount = s->samples==VK_SAMPLE_COUNT_1_BIT?2:3; fi.pAttachments = views;
        fi.width = s->swapchainExtent.width; fi.height = s->swapchainExtent.height; fi.layers = 1;
        VkFramebuffer fb{}; if (vkCreateFramebuffer(s->device, &fi, nullptr, &fb) != VK_SUCCESS) return false;
        s->framebuffers.push_back(fb);
    }
    if(s->pipelineLayout) CreatePipelines(s);
    return true;
}
} // namespace

VulkanBackend::VulkanBackend() : impl_(new Impl()) {}
VulkanBackend::~VulkanBackend() { delete impl_; }

bool VulkanBackend::Init(const ContextConfig& config) {
    impl_->window = static_cast<GLFWwindow*>(config.window);
    impl_->width = config.width;
    impl_->height = config.height;

    if (glfwVulkanSupported() != GLFW_TRUE) {
        std::fprintf(stderr, "[meowyrender][Vulkan] Vulkan not supported by GLFW\n");
        return false;
    }

    // --- Instance ---
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "meowyrender";
    app.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

#if defined(__APPLE__) && defined(MEOWY_VULKAN_MOLTENVK)
    // MoltenVK requires the portability enumeration extension + flag.
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    impl_->usingMoltenVK = true;
    std::snprintf(impl_->name, sizeof(impl_->name), "Vulkan (MoltenVK)");
#else
    std::snprintf(impl_->name, sizeof(impl_->name), "Vulkan");
#endif

    ici.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&ici, nullptr, &impl_->instance) != VK_SUCCESS) {
        std::fprintf(stderr, "[meowyrender][Vulkan] vkCreateInstance failed\n");
        return false;
    }

    // --- Surface (GLFW creates the platform surface, incl. Metal via MoltenVK) ---
    if (glfwCreateWindowSurface(impl_->instance, impl_->window, nullptr,
                                &impl_->surface) != VK_SUCCESS) {
        std::fprintf(stderr, "[meowyrender][Vulkan] surface creation failed\n");
        return false;
    }

    // --- Physical device selection ---
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(impl_->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::fprintf(stderr, "[meowyrender][Vulkan] no Vulkan devices\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(impl_->instance, &deviceCount, devices.data());
    impl_->physicalDevice = devices[0]; // pick the first; can be scored later.

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(impl_->physicalDevice, &props);
    std::printf("[meowyrender][Vulkan] device: %s\n", props.deviceName);

    // --- Queue family (graphics + present) ---
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(impl_->physicalDevice, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(impl_->physicalDevice, &queueCount, queues.data());
    bool found = false;
    for (uint32_t i = 0; i < queueCount; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(impl_->physicalDevice, i, impl_->surface, &present);
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            impl_->graphicsQueueFamily = i;
            found = true;
            break;
        }
    }
    if (!found) {
        std::fprintf(stderr, "[meowyrender][Vulkan] no graphics+present queue\n");
        return false;
    }

    // --- Logical device ---
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = impl_->graphicsQueueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    std::vector<const char*> deviceExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t extensionCount=0;vkEnumerateDeviceExtensionProperties(impl_->physicalDevice,nullptr,&extensionCount,nullptr);
    std::vector<VkExtensionProperties> supportedExtensions(extensionCount);vkEnumerateDeviceExtensionProperties(impl_->physicalDevice,nullptr,&extensionCount,supportedExtensions.data());
    for(const auto& extension:supportedExtensions)if(std::strcmp(extension.extensionName,VK_IMG_FORMAT_PVRTC_EXTENSION_NAME)==0){deviceExts.push_back(VK_IMG_FORMAT_PVRTC_EXTENSION_NAME);impl_->pvrtc=true;}
#if defined(__APPLE__) && defined(MEOWY_VULKAN_MOLTENVK)
    deviceExts.push_back("VK_KHR_portability_subset");
#endif

    VkDeviceCreateInfo dci{};
    VkPhysicalDeviceFeatures supported{},enabled{};vkGetPhysicalDeviceFeatures(impl_->physicalDevice,&supported);
    enabled.textureCompressionBC=supported.textureCompressionBC;enabled.textureCompressionETC2=supported.textureCompressionETC2;
    enabled.textureCompressionASTC_LDR=supported.textureCompressionASTC_LDR;enabled.samplerAnisotropy=supported.samplerAnisotropy;
    dci.pEnabledFeatures=&enabled;
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
    dci.ppEnabledExtensionNames = deviceExts.data();

    if (vkCreateDevice(impl_->physicalDevice, &dci, nullptr, &impl_->device) != VK_SUCCESS) {
        std::fprintf(stderr, "[meowyrender][Vulkan] vkCreateDevice failed\n");
        return false;
    }
    vkGetDeviceQueue(impl_->device, impl_->graphicsQueueFamily, 0, &impl_->graphicsQueue);
    if(config.configFlags&0x20) {
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(impl_->physicalDevice,&properties);
        VkImageFormatProperties depthProperties{};auto result=vkGetPhysicalDeviceImageFormatProperties(impl_->physicalDevice,VK_FORMAT_D32_SFLOAT,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,0,&depthProperties);
        if(result==VK_SUCCESS&&(properties.limits.framebufferColorSampleCounts&depthProperties.sampleCounts&VK_SAMPLE_COUNT_4_BIT))impl_->samples=VK_SAMPLE_COUNT_4_BIT;
    }
    if (!CreateSwapchain(impl_)) return false;
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = impl_->graphicsQueueFamily;
    if (vkCreateCommandPool(impl_->device, &pci, nullptr, &impl_->commandPool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = impl_->commandPool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(impl_->device, &cai, &impl_->commandBuffer) != VK_SUCCESS) return false;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(impl_->device, &sci, nullptr, &impl_->imageAvailable);
    vkCreateSemaphore(impl_->device, &sci, nullptr, &impl_->renderFinished);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(impl_->device, &fci, nullptr, &impl_->frameFence);
    VkDescriptorSetLayoutBinding bindings[10]{};
    for(unsigned int i=0;i<8;++i)bindings[i]={i,i==1?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==7?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,i==7?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    bindings[1].stageFlags|=VK_SHADER_STAGE_VERTEX_BIT;
    // Binding 8: environment cubemap for image-based lighting (fragment stage).
    bindings[8]={8,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    // Binding 9: directional shadow map (fragment stage).
    bindings[9]={9,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount=10; li.pBindings=bindings;
    Check(vkCreateDescriptorSetLayout(impl_->device,&li,nullptr,&impl_->descriptorLayout), "descriptor layout");
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,24576},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4096},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4096}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dpi.maxSets=4096; dpi.poolSizeCount=3; dpi.pPoolSizes=sizes;
    Check(vkCreateDescriptorPool(impl_->device,&dpi,nullptr,&impl_->descriptorPool), "descriptor pool");
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT,0,128};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount=1; pli.pSetLayouts=&impl_->descriptorLayout; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&range;
    Check(vkCreatePipelineLayout(impl_->device,&pli,nullptr,&impl_->pipelineLayout), "pipeline layout");
    CreatePipelines(impl_);
    const unsigned char white[]={255,255,255,255};
    impl_->whiteTex=CreateTexture(white,1,1,PixelFormat::Uncompressed_R8G8B8A8);
    // 1x1 white cubemap so binding 8 (samplerCube) always validates when IBL
    // is disabled; the shader ignores it (hasEnv==0) in that case.
    const unsigned char whiteCubeFaces[6*4]={255,255,255,255, 255,255,255,255, 255,255,255,255,
                                             255,255,255,255, 255,255,255,255, 255,255,255,255};
    impl_->whiteCube=CreateCubemap(whiteCubeFaces,1);
    return true;
}

void VulkanBackend::Shutdown() {
    if (impl_->device) vkDeviceWaitIdle(impl_->device);
    while(!impl_->programs.empty()) DestroyShaderProgram(impl_->programs.begin()->first);
    while(!impl_->targets.empty()) DestroyFramebuffer(impl_->targets.begin()->first,0);
    for(auto pipeline:impl_->targetPipelines) if(pipeline) vkDestroyPipeline(impl_->device,pipeline,nullptr);
    for(auto pipeline:impl_->targetDepthPipelines) if(pipeline) vkDestroyPipeline(impl_->device,pipeline,nullptr);
    for(auto pipeline:impl_->targetDepthNoWritePipelines) if(pipeline) vkDestroyPipeline(impl_->device,pipeline,nullptr);
    if(impl_->targetPass) vkDestroyRenderPass(impl_->device,impl_->targetPass,nullptr);
    if(impl_->targetLoadPass) vkDestroyRenderPass(impl_->device,impl_->targetLoadPass,nullptr);
    while(!impl_->textures.empty()) DestroyTexture(impl_->textures.begin()->first);
    for(auto& [handle,mb]:impl_->meshBuffers){ if(mb.buffer) vkDestroyBuffer(impl_->device,mb.buffer,nullptr); if(mb.memory) vkFreeMemory(impl_->device,mb.memory,nullptr); }
    impl_->meshBuffers.clear();
    for(auto b:impl_->frameBuffers){ vkDestroyBuffer(impl_->device,b.buffer,nullptr); vkFreeMemory(impl_->device,b.memory,nullptr); }
    impl_->frameBuffers.clear();
    if (impl_->frameFence) vkDestroyFence(impl_->device, impl_->frameFence, nullptr);
    if (impl_->renderFinished) vkDestroySemaphore(impl_->device, impl_->renderFinished, nullptr);
    if (impl_->imageAvailable) vkDestroySemaphore(impl_->device, impl_->imageAvailable, nullptr);
    if (impl_->commandPool) vkDestroyCommandPool(impl_->device, impl_->commandPool, nullptr);
    if (impl_->device) DestroySwapchain(impl_);
    if(impl_->pipelineLayout) vkDestroyPipelineLayout(impl_->device,impl_->pipelineLayout,nullptr);
    if(impl_->decodePipeline)vkDestroyPipeline(impl_->device,impl_->decodePipeline,nullptr);
    if(impl_->decodePipelineLayout)vkDestroyPipelineLayout(impl_->device,impl_->decodePipelineLayout,nullptr);
    if(impl_->decodeLayout)vkDestroyDescriptorSetLayout(impl_->device,impl_->decodeLayout,nullptr);
    if(impl_->shadowPipeline) vkDestroyPipeline(impl_->device,impl_->shadowPipeline,nullptr);
    if(impl_->shadowFramebuffer) vkDestroyFramebuffer(impl_->device,impl_->shadowFramebuffer,nullptr);
    if(impl_->shadowPass) vkDestroyRenderPass(impl_->device,impl_->shadowPass,nullptr);
    if(impl_->shadowView) vkDestroyImageView(impl_->device,impl_->shadowView,nullptr);
    if(impl_->shadowImage) vkDestroyImage(impl_->device,impl_->shadowImage,nullptr);
    if(impl_->shadowMemory) vkFreeMemory(impl_->device,impl_->shadowMemory,nullptr);
    if(impl_->shadowDepthView) vkDestroyImageView(impl_->device,impl_->shadowDepthView,nullptr);
    if(impl_->shadowDepthImage) vkDestroyImage(impl_->device,impl_->shadowDepthImage,nullptr);
    if(impl_->shadowDepthMemory) vkFreeMemory(impl_->device,impl_->shadowDepthMemory,nullptr);
    if(impl_->shadowSampler) vkDestroySampler(impl_->device,impl_->shadowSampler,nullptr);
    if(impl_->descriptorPool) vkDestroyDescriptorPool(impl_->device,impl_->descriptorPool,nullptr);
    if(impl_->descriptorLayout) vkDestroyDescriptorSetLayout(impl_->device,impl_->descriptorLayout,nullptr);
    if (impl_->device) vkDestroyDevice(impl_->device, nullptr);
    if (impl_->surface) vkDestroySurfaceKHR(impl_->instance, impl_->surface, nullptr);
    if (impl_->instance) vkDestroyInstance(impl_->instance, nullptr);
    impl_->device = VK_NULL_HANDLE;
    impl_->surface = VK_NULL_HANDLE;
    impl_->instance = VK_NULL_HANDLE;
}

void VulkanBackend::Resize(int width, int height) {
    impl_->width = width;
    impl_->height = height;
}

void VulkanBackend::BeginFrame() {
    impl_->activeTarget=0;
    int w=0,h=0; glfwGetFramebufferSize(impl_->window,&w,&h);
    if(w<=0||h<=0) return;
    vkWaitForFences(impl_->device, 1, &impl_->frameFence, VK_TRUE, UINT64_MAX);
    for(auto b:impl_->frameBuffers){ vkDestroyBuffer(impl_->device,b.buffer,nullptr); vkFreeMemory(impl_->device,b.memory,nullptr); }
    impl_->frameBuffers.clear();
    if(!impl_->frameDescriptors.empty()) vkFreeDescriptorSets(impl_->device,impl_->descriptorPool,static_cast<uint32_t>(impl_->frameDescriptors.size()),impl_->frameDescriptors.data());
    impl_->frameDescriptors.clear();
    VkResult result = vkAcquireNextImageKHR(impl_->device, impl_->swapchain, UINT64_MAX,
                                            impl_->imageAvailable, VK_NULL_HANDLE, &impl_->imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(impl_->device); DestroySwapchain(impl_); CreateSwapchain(impl_);
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;
    vkResetFences(impl_->device, 1, &impl_->frameFence);
    vkResetCommandBuffer(impl_->commandBuffer, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(impl_->commandBuffer, &bi);
    impl_->frameActive = true; impl_->passActive = false; impl_->acquireConsumed=false; impl_->captured=false;
}
void VulkanBackend::Clear(Color color) {
    impl_->clearColor = color;
    if (!impl_->frameActive) return;
    BeginScreenPass(impl_);
    VkClearAttachment clears[2]{};
    clears[0].aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    clears[0].clearValue.color={{color.r/255.f,color.g/255.f,color.b/255.f,color.a/255.f}};
    clears[1].aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT; clears[1].clearValue.depthStencil={1,0};
    VkClearRect rect{{{0,0},TargetExtent(impl_)},0,1};
    vkCmdClearAttachments(impl_->commandBuffer,2,clears,1,&rect);
}
void VulkanBackend::EndFrame() {
    if (!impl_->frameActive) return;
    if(impl_->activeTarget) BindFramebuffer(0,0,0);
    if (!impl_->passActive && !impl_->captured) Clear(impl_->clearColor);
    if (impl_->passActive) vkCmdEndRenderPass(impl_->commandBuffer);
    vkEndCommandBuffer(impl_->commandBuffer);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &impl_->imageAvailable; submit.pWaitDstStageMask = &waitStage;
    if(impl_->acquireConsumed) submit.waitSemaphoreCount=0;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &impl_->commandBuffer;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &impl_->renderFinished;
    vkQueueSubmit(impl_->graphicsQueue, 1, &submit, impl_->frameFence);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1; present.pWaitSemaphores = &impl_->renderFinished;
    present.swapchainCount = 1; present.pSwapchains = &impl_->swapchain; present.pImageIndices = &impl_->imageIndex;
    VkResult result = vkQueuePresentKHR(impl_->graphicsQueue, &present);
    // Serialize this first implementation so presentation has consumed the
    // binary semaphore before the next frame reuses it.
    Check(vkQueueWaitIdle(impl_->graphicsQueue), "wait present");
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(impl_->device); DestroySwapchain(impl_); CreateSwapchain(impl_);
    }
    impl_->frameActive = false; impl_->passActive = false;
}

void VulkanBackend::SetProjection(const Matrix& p) { impl_->projection = p; }
void VulkanBackend::SetViewport(Rectangle viewport) {impl_->viewport=viewport;}
void VulkanBackend::SetModelview(const Matrix& m) { impl_->modelview = m; }
void VulkanBackend::SetScissor(bool enabled, int x, int y, int w, int h) {
    impl_->scissorEnabled=enabled; impl_->scissorX=x; impl_->scissorY=y; impl_->scissorW=w; impl_->scissorH=h;
}
void VulkanBackend::SetBlendMode(int mode) {impl_->blendMode=std::clamp(mode,0,2);}
void VulkanBackend::SetDepthTest(bool enabled) { impl_->depthEnabled=enabled; }
void VulkanBackend::SetDepthMask(bool enabled) { impl_->depthMask=enabled; }
bool VulkanBackend::SupportsGpuSkinning() const {return impl_->activeProgram==0;}
void VulkanBackend::SetSkinning(const Matrix* matrices,int count) {
    impl_->bones.clear();if(!matrices||count<=0)return;
    impl_->bones.resize(count);for(int i=0;i<count;++i)impl_->bones[i]=MatrixTranspose(matrices[i]);
}
void VulkanBackend::SetSurface(const Surface& surface) {impl_->materialSurface=surface;}

// ---------------------------------------------------------------------------
// Directional shadow mapping: a depth-only pass into a sampleable D32 image.
// ---------------------------------------------------------------------------
namespace {
template <typename T> VkImageView MakeShadowDepthAttachment(T* s,int resolution,VkImage& img,VkDeviceMemory& mem) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_D32_SFLOAT;
    ii.extent={static_cast<uint32_t>(resolution),static_cast<uint32_t>(resolution),1};
    ii.mipLevels=1; ii.arrayLayers=1; ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    Check(vkCreateImage(s->device,&ii,nullptr,&img),"shadow depth image");
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(s->device,img,&req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size;
    ai.memoryTypeIndex=MemoryType(s,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(s->device,&ai,nullptr,&mem),"shadow depth memory");
    Check(vkBindImageMemory(s->device,img,mem,0),"bind shadow depth");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=img;
    vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
    VkImageView view{}; Check(vkCreateImageView(s->device,&vi,nullptr,&view),"shadow depth view");
    return view;
}

template <typename T> void CreateShadowResources(T* s, int resolution) {
    // The sampled shadow map is an R32F COLOR image (shadow.frag writes linear
    // light-space depth into it). A companion D32 depth attachment provides the
    // depth test so nearest occluders win. Sampling a color texture avoids
    // MoltenVK's depth-attachment sampling quirks.
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_R32_SFLOAT;
    ii.extent={static_cast<uint32_t>(resolution),static_cast<uint32_t>(resolution),1};
    ii.mipLevels=1; ii.arrayLayers=1; ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    Check(vkCreateImage(s->device,&ii,nullptr,&s->shadowImage),"shadow image");
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(s->device,s->shadowImage,&req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size;
    ai.memoryTypeIndex=MemoryType(s,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(s->device,&ai,nullptr,&s->shadowMemory),"shadow memory");
    Check(vkBindImageMemory(s->device,s->shadowImage,s->shadowMemory,0),"bind shadow");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=s->shadowImage;
    vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=VK_FORMAT_R32_SFLOAT;
    vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Check(vkCreateImageView(s->device,&vi,nullptr,&s->shadowView),"shadow view");
    if(!s->shadowSampler) {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter=si.minFilter=VK_FILTER_NEAREST;
        si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Check(vkCreateSampler(s->device,&si,nullptr,&s->shadowSampler),"shadow sampler");
    }
    // Companion depth attachment (transient).
    VkImageView depthView=MakeShadowDepthAttachment(s,resolution,s->shadowDepthImage,s->shadowDepthMemory);
    s->shadowDepthView=depthView;

    // Render pass: R32F color (store, -> shader read) + D32 depth (transient).
    VkAttachmentDescription atts[2]{};
    atts[0].format=VK_FORMAT_R32_SFLOAT; atts[0].samples=VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; atts[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1].format=VK_FORMAT_D32_SFLOAT; atts[1].samples=VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; atts[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; atts[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount=1; sub.pColorAttachments=&colorRef; sub.pDepthStencilAttachment=&depthRef;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass=VK_SUBPASS_EXTERNAL; deps[0].dstSubpass=0;
    deps[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass=0; deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rp.attachmentCount=2; rp.pAttachments=atts;
    rp.subpassCount=1; rp.pSubpasses=&sub; rp.dependencyCount=2; rp.pDependencies=deps;
    Check(vkCreateRenderPass(s->device,&rp,nullptr,&s->shadowPass),"shadow render pass");
    VkImageView fbViews[2]={s->shadowView,depthView};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass=s->shadowPass;
    fi.attachmentCount=2; fi.pAttachments=fbViews; fi.width=resolution; fi.height=resolution; fi.layers=1;
    Check(vkCreateFramebuffer(s->device,&fi,nullptr,&s->shadowFramebuffer),"shadow framebuffer");
    // Pipeline: shadow.vert + shadow.frag, one R32F color target, depth test on.
    VkShaderModule modules[2]{}; VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize=sizeof(shadow_vert); mi.pCode=shadow_vert; Check(vkCreateShaderModule(s->device,&mi,nullptr,&modules[0]),"shadow vs");
    mi.codeSize=sizeof(shadow_frag); mi.pCode=shadow_frag; Check(vkCreateShaderModule(s->device,&mi,nullptr,&modules[1]),"shadow fs");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=modules[0]; stages[0].pName="main";
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=modules[1]; stages[1].pName="main";
    VkVertexInputBindingDescription vbind[]={{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX},{1,sizeof(Matrix),VK_VERTEX_INPUT_RATE_INSTANCE}};
    VkVertexInputAttributeDescription vattr[]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,x)},{1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,u)},{2,0,VK_FORMAT_R8G8B8A8_UNORM,offsetof(Vertex,r)},
        {3,1,VK_FORMAT_R32G32B32A32_SFLOAT,0},{4,1,VK_FORMAT_R32G32B32A32_SFLOAT,16},{5,1,VK_FORMAT_R32G32B32A32_SFLOAT,32},{6,1,VK_FORMAT_R32G32B32A32_SFLOAT,48},{7,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,joints)},{8,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,weights)},{9,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,nx)}};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vin.vertexBindingDescriptionCount=2; vin.pVertexBindingDescriptions=vbind; vin.vertexAttributeDescriptionCount=10; vin.pVertexAttributeDescriptions=vattr;
    VkPipelineInputAssemblyStateCreateInfo asm2{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    // Occluders are triangle lists. Leaving this zero-initialized selects
    // VK_PRIMITIVE_TOPOLOGY_POINT_LIST (enum 0), which rasterizes only the
    // vertices -- the root cause of the shadow map storing depth at a handful of
    // vertex points instead of filled occluder area.
    asm2.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.lineWidth=1;
    rs.depthBiasEnable=VK_TRUE; rs.depthBiasConstantFactor=1.25f; rs.depthBiasSlopeFactor=1.75f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount=1; cb.pAttachments=&blend;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dyn;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pi.stageCount=2; pi.pStages=stages;
    pi.pVertexInputState=&vin; pi.pInputAssemblyState=&asm2; pi.pViewportState=&vp; pi.pRasterizationState=&rs;
    pi.pMultisampleState=&ms; pi.pDepthStencilState=&ds; pi.pColorBlendState=&cb; pi.pDynamicState=&dynamic;
    pi.layout=s->pipelineLayout; pi.renderPass=s->shadowPass;
    Check(vkCreateGraphicsPipelines(s->device,VK_NULL_HANDLE,1,&pi,nullptr,&s->shadowPipeline),"shadow pipeline");
    vkDestroyShaderModule(s->device,modules[0],nullptr); vkDestroyShaderModule(s->device,modules[1],nullptr);
}
} // namespace

void VulkanBackend::BeginShadowPass(const Matrix& lightViewProj, int resolution) {
    resolution = resolution > 0 ? resolution : 1024;
    if(impl_->shadowImage==VK_NULL_HANDLE || resolution!=impl_->shadowResolution) {
        // (Re)create shadow resources at the requested resolution.
        vkDeviceWaitIdle(impl_->device);
        if(impl_->shadowFramebuffer) vkDestroyFramebuffer(impl_->device,impl_->shadowFramebuffer,nullptr);
        if(impl_->shadowPass) vkDestroyRenderPass(impl_->device,impl_->shadowPass,nullptr);
        if(impl_->shadowPipeline) vkDestroyPipeline(impl_->device,impl_->shadowPipeline,nullptr);
        if(impl_->shadowView) vkDestroyImageView(impl_->device,impl_->shadowView,nullptr);
        if(impl_->shadowImage) vkDestroyImage(impl_->device,impl_->shadowImage,nullptr);
        if(impl_->shadowMemory) vkFreeMemory(impl_->device,impl_->shadowMemory,nullptr);
        if(impl_->shadowDepthView) vkDestroyImageView(impl_->device,impl_->shadowDepthView,nullptr);
        if(impl_->shadowDepthImage) vkDestroyImage(impl_->device,impl_->shadowDepthImage,nullptr);
        if(impl_->shadowDepthMemory) vkFreeMemory(impl_->device,impl_->shadowDepthMemory,nullptr);
        impl_->shadowFramebuffer=VK_NULL_HANDLE; impl_->shadowPass=VK_NULL_HANDLE; impl_->shadowPipeline=VK_NULL_HANDLE;
        impl_->shadowView=VK_NULL_HANDLE; impl_->shadowImage=VK_NULL_HANDLE; impl_->shadowMemory=VK_NULL_HANDLE;
        impl_->shadowDepthView=VK_NULL_HANDLE; impl_->shadowDepthImage=VK_NULL_HANDLE; impl_->shadowDepthMemory=VK_NULL_HANDLE;
        CreateShadowResources(impl_,resolution);
        impl_->shadowResolution=resolution;
    }
    if(!impl_->frameActive) return;
    // Close the active screen pass before recording the depth pass.
    if(impl_->passActive) { vkCmdEndRenderPass(impl_->commandBuffer); impl_->passActive=false; }
    impl_->shadowMatrix=lightViewProj;
    impl_->shadowActive=true;

    VkClearValue clears[2]{}; clears[0].color={{1.0f,1.0f,1.0f,1.0f}}; clears[1].depthStencil={1.0f,0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass=impl_->shadowPass; begin.framebuffer=impl_->shadowFramebuffer;
    begin.renderArea.extent={static_cast<uint32_t>(resolution),static_cast<uint32_t>(resolution)};
    begin.clearValueCount=2; begin.pClearValues=clears;
    vkCmdBeginRenderPass(impl_->commandBuffer,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0,0,(float)resolution,(float)resolution,0,1};
    VkRect2D sc{{0,0},{static_cast<uint32_t>(resolution),static_cast<uint32_t>(resolution)}};
    vkCmdSetViewport(impl_->commandBuffer,0,1,&vp); vkCmdSetScissor(impl_->commandBuffer,0,1,&sc);
    vkCmdBindPipeline(impl_->commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,impl_->shadowPipeline);
    // Push the light view-projection into the vertex push-constant slot.
    Matrix transforms[]={MatrixTranspose(lightViewProj),MatrixTranspose(MatrixIdentity())};
    vkCmdPushConstants(impl_->commandBuffer,impl_->pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(transforms),transforms);
}

void VulkanBackend::EndShadowPass() {
    if(!impl_->shadowActive) return;
    if(impl_->frameActive) {
        vkCmdEndRenderPass(impl_->commandBuffer);
        // The render pass declares an external subpass dependency that should
        // transition the R32F target to SHADER_READ_ONLY and make the color
        // writes available. Under MoltenVK that dependency alone left the store
        // effectively invisible to later sampling/transfer (occluder fragments
        // read back empty). Issue an explicit barrier so the color-attachment
        // writes are flushed and made available to both fragment sampling (the
        // lit pass) and transfer reads (host readback) regardless of how the
        // driver realizes the subpass dependency.
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image=impl_->shadowImage;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(impl_->commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,nullptr,0,nullptr,1,&barrier);
    }
    impl_->passActive=false;
    impl_->shadowActive=false;
    impl_->hasShadowMap=true;
}

void VulkanBackend::ClearShadowMap() { impl_->hasShadowMap=false; }

// ---------------------------------------------------------------------------
// Persistent GPU mesh cache: device-resident vertex buffers reused per frame.
// ---------------------------------------------------------------------------
unsigned int VulkanBackend::UploadMeshBuffer(const Vertex* verts, std::size_t count) {
    if(!verts || count==0 || !impl_->device) return 0;
    Impl::MeshBuffer mb{};
    // Reuse the shared buffer allocator (host-visible, coherent) and keep it
    // resident across frames rather than recycling it in the per-frame pool.
    Impl::Buffer tmp{}; MakeBuffer(impl_,tmp,count*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,verts);
    mb.buffer=tmp.buffer; mb.memory=tmp.memory;
    const unsigned int handle=impl_->nextMeshBuffer++;
    impl_->meshBuffers[handle]=mb;
    return handle;
}
void VulkanBackend::DestroyMeshBuffer(unsigned int handle) {
    auto it=impl_->meshBuffers.find(handle);
    if(it==impl_->meshBuffers.end()) return;
    if(impl_->device) vkDeviceWaitIdle(impl_->device);
    if(it->second.buffer) vkDestroyBuffer(impl_->device,it->second.buffer,nullptr);
    if(it->second.memory) vkFreeMemory(impl_->device,it->second.memory,nullptr);
    impl_->meshBuffers.erase(it);
}
bool VulkanBackend::DrawMeshBuffer(unsigned int handle, std::size_t count,
                                   unsigned int textureId, const Matrix* transforms, int instances) {
    auto it=impl_->meshBuffers.find(handle);
    if(it==impl_->meshBuffers.end() || impl_->activeProgram) return false; // caller falls back
    impl_->instances.resize(std::max(1,instances));
    for(int i=0;i<instances;++i) impl_->instances[i]=MatrixTranspose(transforms[i]);
    if(instances<=0) impl_->instances[0]=MatrixTranspose(MatrixIdentity());
    impl_->boundMeshBuffer=it->second.buffer;
    impl_->boundMeshBufferValid=true;
    // DrawVertices reads boundMeshBuffer instead of streaming; pass a non-null
    // sentinel so the early-out (!vertices) doesn't trigger.
    static const Vertex sentinel{};
    try { DrawVertices(&sentinel,count,DrawMode::Triangles,textureId); }
    catch(...) { impl_->boundMeshBuffer=VK_NULL_HANDLE; impl_->boundMeshBufferValid=false; impl_->instances.clear(); throw; }
    impl_->boundMeshBuffer=VK_NULL_HANDLE;
    impl_->boundMeshBufferValid=false;
    impl_->instances.clear();
    return true;
}

void VulkanBackend::DrawVertices(const Vertex* vertices, std::size_t count, DrawMode mode, unsigned int textureId) {
    if(!impl_->frameActive||!vertices||!count) return;

    // Shadow depth pass: the pipeline + push-constants + render pass are set by
    // BeginShadowPass. Bind the vertex/instance/bone buffers and a descriptor
    // set (binding 7 = bones) that shadow.vert reads, then draw.
    if(impl_->shadowActive) {
        VkCommandBuffer cmd=impl_->commandBuffer;
        Impl::Buffer vbuf{}; MakeBuffer(impl_,vbuf,count*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices);
        impl_->frameBuffers.push_back(vbuf);
        Matrix identity=MatrixIdentity();
        Impl::Buffer instanceBuffer{};
        MakeBuffer(impl_,instanceBuffer,impl_->instances.empty()?sizeof(Matrix):impl_->instances.size()*sizeof(Matrix),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,impl_->instances.empty()?&identity:impl_->instances.data());
        impl_->frameBuffers.push_back(instanceBuffer);
        Impl::Buffer bones{};
        size_t boneBytes=impl_->bones.empty()?sizeof(Matrix):impl_->bones.size()*sizeof(Matrix);
        MakeBuffer(impl_,bones,boneBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,impl_->bones.empty()?&identity:impl_->bones.data());
        impl_->frameBuffers.push_back(bones);
        // Minimal descriptor set: shadow.vert only reads bones (binding 7); the
        // other bindings must still be valid images/buffers for the layout.
        VkDescriptorSet descriptor{};
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool=impl_->descriptorPool; alloc.descriptorSetCount=1; alloc.pSetLayouts=&impl_->descriptorLayout;
        Check(vkAllocateDescriptorSets(impl_->device,&alloc,&descriptor),"shadow descriptors");
        impl_->frameDescriptors.push_back(descriptor);
        auto white=impl_->textures.find(impl_->whiteTex);
        auto whiteCubeIt=impl_->textures.find(impl_->whiteCube);
        VkDescriptorImageInfo img{white->second.sampler,white->second.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo cubeImg{whiteCubeIt->second.sampler,whiteCubeIt->second.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        Impl::Buffer ubo{}; MakeBuffer(impl_,ubo,16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,nullptr); impl_->frameBuffers.push_back(ubo);
        VkDescriptorBufferInfo uboInfo{ubo.buffer,0,16};
        VkDescriptorBufferInfo boneInfo{bones.buffer,0,boneBytes};
        VkWriteDescriptorSet writes[10]{};
        for(auto& w:writes){w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=descriptor;w.descriptorCount=1;}
        writes[0].dstBinding=0;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[0].pImageInfo=&img;
        writes[1].dstBinding=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[1].pBufferInfo=&uboInfo;
        for(int i=2;i<7;++i){writes[i].dstBinding=i;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&img;}
        writes[7].dstBinding=7;writes[7].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[7].pBufferInfo=&boneInfo;
        writes[8].dstBinding=8;writes[8].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[8].pImageInfo=&cubeImg;
        writes[9].dstBinding=9;writes[9].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[9].pImageInfo=&img;
        vkUpdateDescriptorSets(impl_->device,10,writes,0,nullptr);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,impl_->pipelineLayout,0,1,&descriptor,0,nullptr);
        VkDeviceSize offset=0;
        // Re-assert the shadow pipeline + dynamic viewport/scissor for the draw
        // (defensive: intervening state changes must not leave them unset).
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,impl_->shadowPipeline);
        VkViewport svp{0,0,(float)impl_->shadowResolution,(float)impl_->shadowResolution,0,1};
        VkRect2D ssc{{0,0},{(uint32_t)impl_->shadowResolution,(uint32_t)impl_->shadowResolution}};
        vkCmdSetViewport(cmd,0,1,&svp); vkCmdSetScissor(cmd,0,1,&ssc);
        Matrix xf[]={MatrixTranspose(impl_->shadowMatrix),MatrixTranspose(MatrixIdentity())};
        vkCmdPushConstants(cmd,impl_->pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(xf),xf);
        vkCmdBindVertexBuffers(cmd,0,1,&vbuf.buffer,&offset);
        vkCmdBindVertexBuffers(cmd,1,1,&instanceBuffer.buffer,&offset);
        vkCmdDraw(cmd,static_cast<uint32_t>(count),impl_->instances.empty()?1:static_cast<uint32_t>(impl_->instances.size()),0,0);
        return;
    }

    // Keep transient allocations bounded even when a frame has thousands of
    // state-changing draws. Submit and resume with LOAD before recycling them.
    if(impl_->frameDescriptors.size()>=2048) {
        SubmitPending(impl_);
        for(auto b:impl_->frameBuffers){vkDestroyBuffer(impl_->device,b.buffer,nullptr);vkFreeMemory(impl_->device,b.memory,nullptr);}
        impl_->frameBuffers.clear();
        Check(vkFreeDescriptorSets(impl_->device,impl_->descriptorPool,static_cast<uint32_t>(impl_->frameDescriptors.size()),impl_->frameDescriptors.data()),"recycle frame descriptors");
        impl_->frameDescriptors.clear();
    }
    BeginScreenPass(impl_);
    auto it=impl_->textures.find(textureId?textureId:impl_->whiteTex);
    if(it==impl_->textures.end()) return;
    // Use the bound persistent GPU mesh buffer when set; otherwise stream a
    // transient vertex buffer from the CPU data.
    VkBuffer vertexBuffer=impl_->boundMeshBuffer;
    if(!impl_->boundMeshBufferValid) {
        Impl::Buffer buffer{}; MakeBuffer(impl_,buffer,count*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices);
        impl_->frameBuffers.push_back(buffer);
        vertexBuffer=buffer.buffer;
    }
    VkCommandBuffer cmd=impl_->commandBuffer;
    // Pick the depth variant: no-depth, test+write, or test-no-write (transparent).
    auto pickDepth=[&](VkPipeline* none,VkPipeline* write,VkPipeline* noWrite){
        if(!impl_->depthEnabled) return none;
        return impl_->depthMask?write:noWrite;
    };
    auto* pipelines=impl_->activeTarget
        ?pickDepth(impl_->targetPipelines,impl_->targetDepthPipelines,impl_->targetDepthNoWritePipelines)
        :pickDepth(impl_->pipelines,impl_->depthPipelines,impl_->depthNoWritePipelines);
    if(impl_->activeProgram) {
        auto& program=impl_->programs.at(impl_->activeProgram);
        bool target=impl_->activeTarget!=0;
        if(!(target?program.target[0]:program.screen[0])) CreatePipelines(impl_,target,&program);
        pipelines=target
            ?pickDepth(program.target,program.targetDepth,program.targetDepthNoWrite)
            :pickDepth(program.screen,program.screenDepth,program.screenDepthNoWrite);
    }
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[impl_->blendMode*3+static_cast<int>(mode)]);
    auto extent=TargetExtent(impl_);
    VkViewport viewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1};
    viewport.x=impl_->viewport.x*extent.width;viewport.y=impl_->viewport.y*extent.height;
    viewport.width*=impl_->viewport.width;viewport.height*=impl_->viewport.height;
    VkRect2D scissor{{0,0},extent};
    if(impl_->scissorEnabled){
        double sx=impl_->activeTarget?1.0:static_cast<double>(extent.width)/std::max(1,detail::State().screenWidth);
        double sy=impl_->activeTarget?1.0:static_cast<double>(extent.height)/std::max(1,detail::State().screenHeight);
        int x=static_cast<int>(std::clamp(impl_->scissorX*sx,0.0,static_cast<double>(extent.width)));
        int y=static_cast<int>(std::clamp(impl_->scissorY*sy,0.0,static_cast<double>(extent.height)));
        int right=static_cast<int>(std::clamp((static_cast<double>(impl_->scissorX)+impl_->scissorW)*sx,static_cast<double>(x),static_cast<double>(extent.width)));
        int bottom=static_cast<int>(std::clamp((static_cast<double>(impl_->scissorY)+impl_->scissorH)*sy,static_cast<double>(y),static_cast<double>(extent.height)));
        if(right==x||bottom==y) return;
        scissor={{x,y},{static_cast<uint32_t>(right-x),static_cast<uint32_t>(bottom-y)}};
    }
    vkCmdSetViewport(cmd,0,1,&viewport); vkCmdSetScissor(cmd,0,1,&scissor);
    Matrix transforms[]={MatrixTranspose(impl_->projection),MatrixTranspose(impl_->modelview)};
    vkCmdPushConstants(cmd,impl_->pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(transforms),transforms);
    VkDescriptorSet descriptor{};
    {
        const auto& surface=impl_->materialSurface;
        struct LitUniforms {Vector4 eye,direction,radiance,ambient,emission;float metallic,roughness;unsigned int enabled,mask;float alphaCutoff;
                            float envIntensity;unsigned int envMips;unsigned int hasEnv;
                            unsigned int hasShadow;unsigned int padB,padC,padD;  // align shadowMatrix to 16 bytes
                            Matrix shadowMatrix;};
        auto vector=[](Vector3 v){return Vector4{v.x,v.y,v.z,0};};
        const bool hasEnv=surface.light.environment!=0;
        LitUniforms light{vector(surface.light.eye),vector(surface.light.direction),vector(surface.light.radiance),vector(surface.light.ambient),vector(surface.emission),surface.metallic,surface.roughness,surface.enabled?1u:0u,surface.mask,surface.alphaCutoff,
                          surface.light.environmentIntensity,static_cast<unsigned int>(std::max(1,surface.light.environmentMips)),hasEnv?1u:0u,
                          impl_->hasShadowMap?1u:0u,0u,0u,0u,
                          MatrixTranspose(impl_->shadowMatrix)};
        const void* values=&light;size_t size=sizeof(light);
        if(impl_->activeProgram){auto& bytes=impl_->programs.at(impl_->activeProgram).shader.values;values=bytes.data();size=bytes.size();}
        Impl::Buffer uniform{}; MakeBuffer(impl_,uniform,size,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,values);
        impl_->frameBuffers.push_back(uniform);
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool=impl_->descriptorPool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&impl_->descriptorLayout;
        Check(vkAllocateDescriptorSets(impl_->device,&allocation,&descriptor),"custom shader descriptors");
        impl_->frameDescriptors.push_back(descriptor);
        VkDescriptorImageInfo image{it->second.sampler,it->second.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo data{uniform.buffer,0,size};
        Matrix identity=MatrixIdentity();Impl::Buffer bones{};
        size_t boneBytes=impl_->bones.empty()?sizeof(Matrix):impl_->bones.size()*sizeof(Matrix);
        MakeBuffer(impl_,bones,boneBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,impl_->bones.empty()?&identity:impl_->bones.data());impl_->frameBuffers.push_back(bones);
        VkDescriptorBufferInfo boneData{bones.buffer,0,boneBytes};
        VkDescriptorImageInfo maps[5]{};
        VkWriteDescriptorSet writes[10]{};
        for(auto& write:writes) {write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=descriptor;write.descriptorCount=1;}
        writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].pImageInfo=&image;
        writes[1].dstBinding=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[1].pBufferInfo=&data;
        for(int i=0;i<5;++i) {
            auto found=impl_->textures.find(surface.maps[i]);if(found==impl_->textures.end())found=impl_->textures.find(impl_->whiteTex);
            maps[i]={found->second.sampler,found->second.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[i+2].dstBinding=i+2;writes[i+2].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i+2].pImageInfo=&maps[i];
        }
        writes[7].dstBinding=7;writes[7].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[7].pBufferInfo=&boneData;
        // Binding 8: environment cubemap (or the 1x1 white cube when IBL off).
        auto envIt=impl_->textures.find(hasEnv?surface.light.environment:impl_->whiteCube);
        if(envIt==impl_->textures.end())envIt=impl_->textures.find(impl_->whiteCube);
        VkDescriptorImageInfo envImage{envIt->second.sampler,envIt->second.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[8].dstBinding=8;writes[8].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[8].pImageInfo=&envImage;
        // Binding 9: directional shadow map. Use the depth image when a shadow
        // pass has produced one; otherwise the 1x1 white texture (hasShadow==0).
        auto whiteIt=impl_->textures.find(impl_->whiteTex);
        VkDescriptorImageInfo shadowImage;
        if(impl_->hasShadowMap && impl_->shadowView)
            shadowImage={impl_->shadowSampler,impl_->shadowView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        else
            shadowImage={whiteIt->second.sampler,whiteIt->second.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[9].dstBinding=9;writes[9].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[9].pImageInfo=&shadowImage;
        vkUpdateDescriptorSets(impl_->device,10,writes,0,nullptr);
    }
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,impl_->pipelineLayout,0,1,&descriptor,0,nullptr);
    VkDeviceSize offset=0; vkCmdBindVertexBuffers(cmd,0,1,&vertexBuffer,&offset);
    Matrix identity=MatrixIdentity();
    Impl::Buffer instanceBuffer{};
    MakeBuffer(impl_,instanceBuffer,impl_->instances.empty()?sizeof(Matrix):impl_->instances.size()*sizeof(Matrix),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,impl_->instances.empty()?&identity:impl_->instances.data());
    impl_->frameBuffers.push_back(instanceBuffer);
    vkCmdBindVertexBuffers(cmd,1,1,&instanceBuffer.buffer,&offset);
    vkCmdDraw(cmd,static_cast<uint32_t>(count),impl_->instances.empty()?1:static_cast<uint32_t>(impl_->instances.size()),0,0);
}

void VulkanBackend::DrawVerticesInstanced(const Vertex* vertices,std::size_t count,unsigned int textureId,const Matrix* transforms,int instances) {
    if(!transforms||instances<=0)return;
    if(impl_->activeProgram) {RenderBackend::DrawVerticesInstanced(vertices,count,textureId,transforms,instances);return;}
    impl_->instances.resize(instances);
    for(int i=0;i<instances;++i)impl_->instances[i]=MatrixTranspose(transforms[i]);
    try {DrawVertices(vertices,count,DrawMode::Triangles,textureId);}catch(...){impl_->instances.clear();throw;}
    impl_->instances.clear();
}

unsigned int VulkanBackend::CreateTexture(const void* pixels, int width, int height, PixelFormat format) {return CreateTextureLayers(pixels,width,height,format,1);}
bool VulkanBackend::SupportsTextureFormat(PixelFormat format) const {
    int value=static_cast<int>(format);VkFormat native=CompressedFormat(format);
    if(native==VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG&&!impl_->pvrtc)return false;
    if(value>=1&&value<=13)native=value>=8?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
    if(native==VK_FORMAT_UNDEFINED)return false;
    VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(impl_->physicalDevice,native,&properties);
    return (properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)!=0;
}
unsigned int VulkanBackend::CreateCubemap(const void* pixels,int size) {return CreateTextureLayers(pixels,size,size,PixelFormat::Uncompressed_R8G8B8A8,6);}
unsigned int VulkanBackend::CreateTextureLayers(const void* pixels,int width,int height,PixelFormat format,int layers) {
    if(width<=0||height<=0||!SupportsTextureFormat(format)) return 0;
    if((format==PixelFormat::Compressed_PVRT_RGB || format==PixelFormat::Compressed_PVRT_RGBA) &&
       (width<8 || height<8 || (width&(width-1)) || (height&(height-1))))return 0;
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(impl_->physicalDevice,&properties);
    if(static_cast<uint32_t>(width)>properties.limits.maxImageDimension2D||static_cast<uint32_t>(height)>properties.limits.maxImageDimension2D)return 0;
    const VkFormat compressed=CompressedFormat(format);
    auto upload=compressed==VK_FORMAT_UNDEFINED?ConvertUpload(pixels,width,height*layers,format):UploadPixels{};
    const size_t bytes=compressed==VK_FORMAT_UNDEFINED?upload.stride(width)*height*layers:CompressedSize(width,height,format)*layers;
    if(compressed!=VK_FORMAT_UNDEFINED&&!pixels)return 0;
    if(compressed==VK_FORMAT_UNDEFINED)pixels=upload.data();
    Impl::Texture texture{}; texture.width=width; texture.height=height;texture.layers=layers;
    texture.format=compressed!=VK_FORMAT_UNDEFINED?compressed:upload.floating?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
    if(compressed==VK_FORMAT_UNDEFINED)for(int size=std::max(width,height); size>1; size/=2) ++texture.levels;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType=VK_IMAGE_TYPE_2D; ci.format=VK_FORMAT_R8G8B8A8_UNORM; ci.extent={static_cast<uint32_t>(width),static_cast<uint32_t>(height),1}; ci.mipLevels=1; ci.arrayLayers=1; ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.mipLevels=texture.levels;ci.arrayLayers=layers;if(layers==6)ci.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.format=texture.format;
    if(texture.format==VK_FORMAT_R8G8B8A8_UNORM)ci.usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    Check(vkCreateImage(impl_->device,&ci,nullptr,&texture.image), "texture image");
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(impl_->device,texture.image,&requirements);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=requirements.size; ai.memoryTypeIndex=MemoryType(impl_,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(impl_->device,&ai,nullptr,&texture.memory), "texture memory"); Check(vkBindImageMemory(impl_->device,texture.image,texture.memory,0), "bind texture");
    std::vector<unsigned char> zero;
    if(!pixels){ zero.resize(static_cast<std::size_t>(width)*height*4*layers); pixels=zero.data(); }
    Impl::Buffer staging{}; MakeBuffer(impl_,staging,bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,pixels);
    Immediate(impl_,[&](VkCommandBuffer cmd){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image=texture.image; barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.subresourceRange.levelCount=texture.levels;barrier.subresourceRange.layerCount=layers;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent=ci.extent;copy.imageSubresource.layerCount=layers;
        vkCmdCopyBufferToImage(cmd,staging.buffer,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    });
    vkDestroyBuffer(impl_->device,staging.buffer,nullptr); vkFreeMemory(impl_->device,staging.memory,nullptr);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=texture.image; vi.viewType=layers==6?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_2D; vi.format=ci.format; vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,texture.levels,0,static_cast<uint32_t>(layers)}; if(format==PixelFormat::Compressed_PVRT_RGB)vi.components.a=VK_COMPONENT_SWIZZLE_ONE;
    Check(vkCreateImageView(impl_->device,&vi,nullptr,&texture.view), "texture view");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.magFilter=si.minFilter=VK_FILTER_LINEAR; si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    Check(vkCreateSampler(impl_->device,&si,nullptr,&texture.sampler), "sampler");
    unsigned int id=impl_->nextTextureId++; impl_->textures.emplace(id,texture); return id;
}
void VulkanBackend::DestroyTexture(unsigned int id) {
    auto it=impl_->textures.find(id); if(it==impl_->textures.end()) return;
    SubmitPending(impl_);
    auto t=it->second;
    vkDestroySampler(impl_->device,t.sampler,nullptr); vkDestroyImageView(impl_->device,t.view,nullptr); vkDestroyImage(impl_->device,t.image,nullptr); vkFreeMemory(impl_->device,t.memory,nullptr); impl_->textures.erase(it);
}
void VulkanBackend::UpdateTexture(unsigned int id, int width, int height, PixelFormat format, const void* pixels) {
    auto it=impl_->textures.find(id);
    if(it==impl_->textures.end() || !pixels || it->second.layers!=1 || !SupportsTextureFormat(format) || width!=it->second.width || height!=it->second.height) return;
    const auto compressed=CompressedFormat(format);
    auto upload=compressed==VK_FORMAT_UNDEFINED?ConvertUpload(pixels,width,height,format):UploadPixels{};
    VkFormat native=compressed!=VK_FORMAT_UNDEFINED?compressed:upload.floating?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
    if(native!=it->second.format)return;
    size_t bytes=compressed==VK_FORMAT_UNDEFINED?upload.stride(width)*height:CompressedSize(width,height,format);
    if(compressed==VK_FORMAT_UNDEFINED)pixels=upload.data();
    SubmitPending(impl_);
    Impl::Buffer staging{}; MakeBuffer(impl_,staging,bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,pixels);
    Immediate(impl_,[&](VkCommandBuffer cmd){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image=it->second.image;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={static_cast<uint32_t>(width),static_cast<uint32_t>(height),1};
        vkCmdCopyBufferToImage(cmd,staging.buffer,it->second.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    });
    vkDestroyBuffer(impl_->device,staging.buffer,nullptr); vkFreeMemory(impl_->device,staging.memory,nullptr);
}
int VulkanBackend::GenTextureMipmaps(unsigned int id) {
    auto it=impl_->textures.find(id); if(it==impl_->textures.end()) return 0;
    auto& texture=it->second;
    VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(impl_->physicalDevice,texture.format,&properties);
    const auto required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if(texture.levels==1||(properties.optimalTilingFeatures&required)!=required)return 1;
    SubmitPending(impl_);
    Immediate(impl_,[&](VkCommandBuffer cmd){
        int width=texture.width,height=texture.height;
        for(uint32_t level=1;level<texture.levels;++level){
            VkImageMemoryBarrier barriers[2]{};
            for(auto& barrier:barriers){
                barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; barrier.image=texture.image;
                barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,static_cast<uint32_t>(texture.layers)};
            }
            barriers[0].subresourceRange.baseMipLevel=level-1; barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            barriers[1].subresourceRange.baseMipLevel=level; barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers);
            int nextWidth=std::max(1,width/2),nextHeight=std::max(1,height/2);
            VkImageBlit blit{}; blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level-1,0,1}; blit.srcOffsets[1]={width,height,1};
            blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1}; blit.dstOffsets[1]={nextWidth,nextHeight,1};
            blit.srcSubresource.layerCount=blit.dstSubresource.layerCount=texture.layers;
            vkCmdBlitImage(cmd,texture.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
            for(auto& barrier:barriers){barrier.oldLayout=barrier.newLayout; barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.srcAccessMask=barrier.dstAccessMask; barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;}
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
            width=nextWidth; height=nextHeight;
        }
    });
    texture.mipmaps=true; ReplaceSampler(impl_,texture);
    return static_cast<int>(texture.levels);
}
void VulkanBackend::SetTextureFilter(unsigned int id, int filter) {
    auto it=impl_->textures.find(id); if(it==impl_->textures.end()) return;
    it->second.filter=filter; ReplaceSampler(impl_,it->second);
}
void VulkanBackend::SetTextureWrap(unsigned int id, int wrap) {
    auto it=impl_->textures.find(id); if(it==impl_->textures.end()) return;
    it->second.wrap=wrap; ReplaceSampler(impl_,it->second);
}
unsigned int VulkanBackend::WhiteTexture() const { return impl_->whiteTex; }

namespace {
void CopyReadback(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, VkBuffer buffer, int width, int height,int face=0) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image=image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,static_cast<uint32_t>(face),1};
    barrier.oldLayout=layout; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,static_cast<uint32_t>(face),1}; copy.imageExtent={static_cast<uint32_t>(width),static_cast<uint32_t>(height),1};
    vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout=layout;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
template<typename T,typename B> Image ReadBuffer(T* s, B buffer, int width,int height,bool bgra) {
    Image image{}; image.width=width; image.height=height;
    const std::size_t bytes=static_cast<std::size_t>(width)*height*4;
    image.data=std::malloc(bytes);
    if(image.data){ void* data{}; Check(vkMapMemory(s->device,buffer.memory,0,bytes,0,&data), "map readback"); std::memcpy(image.data,data,bytes); vkUnmapMemory(s->device,buffer.memory);
        auto* p=static_cast<unsigned char*>(image.data); if(bgra) for(std::size_t i=0;i<bytes;i+=4) std::swap(p[i],p[i+2]); }
    vkDestroyBuffer(s->device,buffer.buffer,nullptr); vkFreeMemory(s->device,buffer.memory,nullptr); return image;
}
template<typename T,typename Texture> Image DecodeTexture(T* s,const Texture& texture) {
    if(!s->decodePipeline) {
        VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=2;li.pBindings=bindings;
        Check(vkCreateDescriptorSetLayout(s->device,&li,nullptr,&s->decodeLayout),"decode descriptor layout");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,8};
        VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pi.setLayoutCount=1;pi.pSetLayouts=&s->decodeLayout;pi.pushConstantRangeCount=1;pi.pPushConstantRanges=&push;
        Check(vkCreatePipelineLayout(s->device,&pi,nullptr,&s->decodePipelineLayout),"decode pipeline layout");
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=sizeof(decode_comp);mi.pCode=decode_comp;VkShaderModule module{};
        Check(vkCreateShaderModule(s->device,&mi,nullptr,&module),"decode shader");
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=s->decodePipelineLayout;
        ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="main";
        VkResult result=vkCreateComputePipelines(s->device,VK_NULL_HANDLE,1,&ci,nullptr,&s->decodePipeline);vkDestroyShaderModule(s->device,module,nullptr);Check(result,"decode pipeline");
    }
    typename T::Buffer output{};MakeBuffer(s,output,static_cast<VkDeviceSize>(texture.width)*texture.height*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=s->descriptorPool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&s->decodeLayout;
    VkDescriptorSet descriptor{};Check(vkAllocateDescriptorSets(s->device,&allocation,&descriptor),"decode descriptor");
    VkDescriptorImageInfo image{texture.sampler,texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo buffer{output.buffer,0,VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[2]{};for(auto& write:writes){write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=descriptor;write.descriptorCount=1;}
    writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[0].pImageInfo=&image;
    writes[1].dstBinding=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&buffer;
    vkUpdateDescriptorSets(s->device,2,writes,0,nullptr);
    Immediate(s,[&](VkCommandBuffer cmd){
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->decodePipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s->decodePipelineLayout,0,1,&descriptor,0,nullptr);
        uint32_t size[]={static_cast<uint32_t>(texture.width),static_cast<uint32_t>(texture.height)};
        vkCmdPushConstants(cmd,s->decodePipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,size);
        vkCmdDispatch(cmd,(size[0]+7)/8,(size[1]+7)/8,1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    });
    vkFreeDescriptorSets(s->device,s->descriptorPool,1,&descriptor);
    return ReadBuffer(s,output,texture.width,texture.height,false);
}
}
Image VulkanBackend::ReadCubemapFace(unsigned int id,int face) {
    auto found=impl_->textures.find(id);if(found==impl_->textures.end()||found->second.layers!=6||face<0||face>=6)return {};
    SubmitPending(impl_);auto& texture=found->second;Impl::Buffer buffer{};
    MakeBuffer(impl_,buffer,static_cast<VkDeviceSize>(texture.width)*texture.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    Immediate(impl_,[&](VkCommandBuffer cmd){CopyReadback(cmd,texture.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,buffer.buffer,texture.width,texture.height,face);});
    return ReadBuffer(impl_,buffer,texture.width,texture.height,false);
}
Image VulkanBackend::ReadTexture(unsigned int id) {
    auto it=impl_->textures.find(id); if(it==impl_->textures.end()) return {};
    SubmitPending(impl_);
    if(it->second.format!=VK_FORMAT_R8G8B8A8_UNORM)return DecodeTexture(impl_,it->second);
    auto& t=it->second; Impl::Buffer buffer{};
    MakeBuffer(impl_,buffer,static_cast<VkDeviceSize>(t.width)*t.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    Immediate(impl_,[&](VkCommandBuffer cmd){ CopyReadback(cmd,t.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,buffer.buffer,t.width,t.height); });
    return ReadBuffer(impl_,buffer,t.width,t.height,false);
}
Image VulkanBackend::ReadShadowMap() {
    // No shadow map recorded -> nothing to read.
    if(impl_->shadowImage==VK_NULL_HANDLE || impl_->shadowResolution<=0) return {};
    // Flush any recorded frame work so the shadow pass's R32F store completes
    // and (per its render-pass external dependency) becomes available; then
    // copy the R32F color target to host. The image sits in
    // SHADER_READ_ONLY_OPTIMAL after EndShadowPass.
    SubmitPending(impl_);
    const int size=impl_->shadowResolution;
    Impl::Buffer buffer{};
    MakeBuffer(impl_,buffer,static_cast<VkDeviceSize>(size)*size*sizeof(float),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    Immediate(impl_,[&](VkCommandBuffer cmd){ CopyReadback(cmd,impl_->shadowImage,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,buffer.buffer,size,size); });
    // Map the raw float depths and pack depth[0,1] -> R byte (255 = cleared/no
    // occluder, <255 = an occluder fragment was stored). G/B/A carry 0/0/255.
    Image image{}; image.width=image.height=size;
    const std::size_t texels=static_cast<std::size_t>(size)*size;
    image.data=std::malloc(texels*4);
    if(image.data) {
        void* mapped{}; Check(vkMapMemory(impl_->device,buffer.memory,0,texels*sizeof(float),0,&mapped),"map shadow readback");
        const float* depth=static_cast<const float*>(mapped);
        auto* out=static_cast<unsigned char*>(image.data);
        for(std::size_t i=0;i<texels;++i) {
            float d=depth[i]; d=d<0.0f?0.0f:(d>1.0f?1.0f:d);
            out[i*4+0]=static_cast<unsigned char>(d*255.0f+0.5f);
            out[i*4+1]=0; out[i*4+2]=0; out[i*4+3]=255;
        }
        vkUnmapMemory(impl_->device,buffer.memory);
    }
    vkDestroyBuffer(impl_->device,buffer.buffer,nullptr); vkFreeMemory(impl_->device,buffer.memory,nullptr);
    return image;
}
Image VulkanBackend::ReadScreen() {
    if(!impl_->frameActive) return {};
    const unsigned int target=impl_->activeTarget;
    if(target) BindFramebuffer(0,0,0);
    BeginScreenPass(impl_);
    vkCmdEndRenderPass(impl_->commandBuffer); impl_->passActive=false;
    int width=impl_->swapchainExtent.width,height=impl_->swapchainExtent.height;
    Impl::Buffer buffer{}; MakeBuffer(impl_,buffer,static_cast<VkDeviceSize>(width)*height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    CopyReadback(impl_->commandBuffer,impl_->swapchainImages[impl_->imageIndex],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,buffer.buffer,width,height);
    Check(vkEndCommandBuffer(impl_->commandBuffer), "end capture command");
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&impl_->commandBuffer;
    si.waitSemaphoreCount=impl_->acquireConsumed?0:1; si.pWaitSemaphores=&impl_->imageAvailable; si.pWaitDstStageMask=&stage;
    Check(vkQueueSubmit(impl_->graphicsQueue,1,&si,VK_NULL_HANDLE), "submit capture"); Check(vkQueueWaitIdle(impl_->graphicsQueue), "wait capture");
    impl_->acquireConsumed=true; impl_->captured=true;
    vkResetCommandBuffer(impl_->commandBuffer,0); VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; Check(vkBeginCommandBuffer(impl_->commandBuffer,&bi), "resume after capture");
    Image result=ReadBuffer(impl_,buffer,width,height,impl_->swapchainFormat==VK_FORMAT_B8G8R8A8_UNORM||impl_->swapchainFormat==VK_FORMAT_B8G8R8A8_SRGB);
    if(target) BindFramebuffer(target,0,0);
    return result;
}

// Offscreen targets share compatible pipelines and preserve their contents
// when uploads or readback suspend command recording.
unsigned int VulkanBackend::CreateFramebuffer(int width,int height,unsigned int* colorTexOut) {
    if (colorTexOut) *colorTexOut = 0;
    if(width<=0 || height<=0) return 0;
    if(!impl_->targetPass) {
        CreateRenderPasses(impl_,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,impl_->targetPass,impl_->targetLoadPass);
        CreatePipelines(impl_,true);
    }
    Impl::Target target; target.extent={static_cast<uint32_t>(width),static_cast<uint32_t>(height)};
    target.color=CreateTexture(nullptr,width,height,PixelFormat::Uncompressed_R8G8B8A8);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType=VK_IMAGE_TYPE_2D; info.format=VK_FORMAT_D32_SFLOAT;
    info.extent={static_cast<uint32_t>(width),static_cast<uint32_t>(height),1}; info.mipLevels=info.arrayLayers=1; info.samples=impl_->samples; info.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    Check(vkCreateImage(impl_->device,&info,nullptr,&target.depth),"offscreen depth");
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(impl_->device,target.depth,&requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize=requirements.size; allocate.memoryTypeIndex=MemoryType(impl_,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(impl_->device,&allocate,nullptr,&target.memory),"offscreen depth memory"); Check(vkBindImageMemory(impl_->device,target.depth,target.memory,0),"offscreen depth bind");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=target.depth; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=info.format; view.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
    Check(vkCreateImageView(impl_->device,&view,nullptr,&target.view),"offscreen depth view");
    view.image=impl_->textures.at(target.color).image; view.format=VK_FORMAT_R8G8B8A8_UNORM; view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Check(vkCreateImageView(impl_->device,&view,nullptr,&target.colorView),"offscreen color view");
    CreateMultisampleColor(impl_,target.multisample,VK_FORMAT_R8G8B8A8_UNORM,target.extent);
    VkImageView attachments[]={impl_->samples==VK_SAMPLE_COUNT_1_BIT?target.colorView:target.multisample.view,target.view,target.colorView};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass=impl_->targetPass; framebuffer.attachmentCount=impl_->samples==VK_SAMPLE_COUNT_1_BIT?2:3; framebuffer.pAttachments=attachments; framebuffer.width=width; framebuffer.height=height; framebuffer.layers=1;
    Check(vkCreateFramebuffer(impl_->device,&framebuffer,nullptr,&target.framebuffer),"offscreen framebuffer");
    unsigned int id=impl_->nextTarget++; impl_->targets.emplace(id,target); if(colorTexOut) *colorTexOut=target.color; return id;
}
void VulkanBackend::DestroyFramebuffer(unsigned int id,unsigned int) {
    auto found=impl_->targets.find(id); if(found==impl_->targets.end()) return;
    if(impl_->activeTarget==id) BindFramebuffer(0,0,0);
    SubmitPending(impl_); auto target=found->second;
    vkDestroyFramebuffer(impl_->device,target.framebuffer,nullptr); vkDestroyImageView(impl_->device,target.view,nullptr);
    vkDestroyImageView(impl_->device,target.colorView,nullptr);
    DestroyAttachment(impl_,target.multisample);
    vkDestroyImage(impl_->device,target.depth,nullptr); vkFreeMemory(impl_->device,target.memory,nullptr);
    DestroyTexture(target.color); impl_->targets.erase(found);
}
void VulkanBackend::BindFramebuffer(unsigned int id,int,int) {
    if(id && !impl_->targets.contains(id)) return;
    if(impl_->passActive) {vkCmdEndRenderPass(impl_->commandBuffer);impl_->passActive=false;}
    impl_->activeTarget=id;
}
unsigned int VulkanBackend::CreateShaderProgram(const char* vs,const char* fs) {
    Impl::Program program;
    try {
        program.shader=CompileShader(vs,fs);
        CreatePipelines(impl_,false,&program);
    } catch(const std::exception& error) {
        for(auto* array:{program.screen,program.screenDepth}) for(int i=0;i<9;++i) if(array[i]) vkDestroyPipeline(impl_->device,array[i],nullptr);
        std::fprintf(stderr,"[meowyrender][Vulkan] shader: %s\n",error.what()); return 0;
    }
    unsigned id=impl_->nextProgram++; impl_->programs.emplace(id,std::move(program));return id;
}
void VulkanBackend::DestroyShaderProgram(unsigned int id) {
    auto found=impl_->programs.find(id);if(found==impl_->programs.end()) return;
    SubmitPending(impl_);
    auto& p=found->second;
    for(auto* array:{p.screen,p.screenDepth,p.screenDepthNoWrite,p.target,p.targetDepth,p.targetDepthNoWrite}) for(int i=0;i<9;++i) if(array[i]) vkDestroyPipeline(impl_->device,array[i],nullptr);
    if(impl_->activeProgram==id) impl_->activeProgram=0;
    impl_->programs.erase(found);
}
int VulkanBackend::GetShaderUniformLocation(unsigned int id,const char* name) {
    auto found=impl_->programs.find(id);if(found==impl_->programs.end()||!name) return -1;
    auto& uniforms=found->second.shader.uniforms;
    for(size_t i=0;i<uniforms.size();++i) {
        auto shortName=uniforms[i].name;
        auto dot=shortName.find_last_of('.');if(dot!=std::string::npos)shortName=shortName.substr(dot+1);
        if(shortName.ends_with("[0]"))shortName.resize(shortName.size()-3);
        if(uniforms[i].name==name||shortName==name)return static_cast<int>(i);
    }
    return -1;
}
void VulkanBackend::SetShaderUniform(unsigned int id,int location,const void* value,int type,int count) {
    auto found=impl_->programs.find(id);if(found==impl_->programs.end()||!value||location<0||count<=0)return;
    auto& shader=found->second.shader;
    if(static_cast<size_t>(location)>=shader.uniforms.size())return;
    auto& uniform=shader.uniforms[location];
    if(type!=uniform.type||count>uniform.size)throw std::invalid_argument("Uniform type or array count mismatch");
    size_t bytes=type==5?64:type==4?4:static_cast<size_t>(type+1)*4;
    size_t stride=uniform.stride>0?uniform.stride:bytes;
    if(uniform.offset<0||uniform.offset+(count-1)*stride+bytes>shader.values.size())throw std::runtime_error("Uniform outside reflected buffer");
    for(int i=0;i<count;++i) {
        void* output=shader.values.data()+uniform.offset+i*stride;
        if(type==5){Matrix matrix;std::memcpy(&matrix,static_cast<const char*>(value)+i*bytes,bytes);matrix=MatrixTranspose(matrix);std::memcpy(output,&matrix,bytes);}
        else std::memcpy(output,static_cast<const char*>(value)+i*bytes,bytes);
    }
}
void VulkanBackend::SetActiveShader(unsigned int id) {
    if(id&&!impl_->programs.contains(id))throw std::invalid_argument("Invalid Vulkan shader");
    impl_->activeProgram=id;
}

const char* VulkanBackend::Name() const { return impl_->name; }

#if defined(MEOWY_WITH_IMGUI)
// Dear ImGui on Vulkan: imgui_impl_glfw for platform/input + imgui_impl_vulkan
// for rendering. We build the renderer against the main swapchain render pass
// (traditional VkRenderPass, no dynamic rendering) and record draw data into
// the backend's single per-frame command buffer inside the active screen pass.
bool VulkanBackend::ImGuiInit(void* glfwWindow) {
    if (!impl_->device || !impl_->renderPass) return false;
    auto* window = static_cast<GLFWwindow*>(glfwWindow);
    if (!window) return false;

    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }

    // A dedicated pool sized for ImGui's font atlas + user texture bindings.
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 128;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(impl_->device, &poolInfo, nullptr, &impl_->imguiPool) != VK_SUCCESS) {
        ImGui::DestroyContext();
        impl_->imguiPool = VK_NULL_HANDLE;
        return false;
    }

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        vkDestroyDescriptorPool(impl_->device, impl_->imguiPool, nullptr);
        impl_->imguiPool = VK_NULL_HANDLE;
        ImGui::DestroyContext();
        return false;
    }

    const uint32_t imageCount = static_cast<uint32_t>(impl_->swapchainImages.size());
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = impl_->instance;
    info.PhysicalDevice = impl_->physicalDevice;
    info.Device = impl_->device;
    info.QueueFamily = impl_->graphicsQueueFamily;
    info.Queue = impl_->graphicsQueue;
    info.DescriptorPool = impl_->imguiPool;
    info.RenderPass = impl_->renderPass;
    info.MinImageCount = imageCount < 2 ? 2 : imageCount;
    info.ImageCount = imageCount < 2 ? 2 : imageCount;
    info.MSAASamples = impl_->samples;
    info.Subpass = 0;
    info.UseDynamicRendering = false;
    if (!ImGui_ImplVulkan_Init(&info)) {
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(impl_->device, impl_->imguiPool, nullptr);
        impl_->imguiPool = VK_NULL_HANDLE;
        ImGui::DestroyContext();
        return false;
    }
    // Upload the font atlas (self-contained in imgui 1.90+: creates its own
    // one-time command buffer internally).
    ImGui_ImplVulkan_CreateFontsTexture();
    impl_->imguiReady = true;
    return true;
}
void VulkanBackend::ImGuiNewFrame() {
    if (!impl_->imguiReady) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
void VulkanBackend::ImGuiRender() {
    if (!impl_->imguiReady) return;
    ImGui::Render();
    if (!impl_->frameActive) return;
    // Ensure the main screen render pass is open so ImGui records into it (and
    // preserves the scene already drawn). BeginScreenPass is a no-op if a pass
    // is already active.
    BeginScreenPass(impl_);
    if (!impl_->passActive) return;
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), impl_->commandBuffer);
}
void VulkanBackend::ImGuiShutdown() {
    if (!impl_->imguiReady) return;
    if (impl_->device) vkDeviceWaitIdle(impl_->device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    if (impl_->imguiPool) {
        vkDestroyDescriptorPool(impl_->device, impl_->imguiPool, nullptr);
        impl_->imguiPool = VK_NULL_HANDLE;
    }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    impl_->imguiReady = false;
}
#endif // MEOWY_WITH_IMGUI

} // namespace meowyrender::backend::vulkan
