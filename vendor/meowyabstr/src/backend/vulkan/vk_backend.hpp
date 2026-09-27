// meowyrender - src/backend/vulkan/vk_backend.hpp  (internal)
#pragma once

#include "backend/render_backend.hpp"

namespace meowyrender::backend::vulkan {

// Vulkan backend. On Apple platforms it runs over MoltenVK (Vulkan-on-Metal)
// when MEOWY_VULKAN_USE_MOLTENVK is enabled. The pimpl hides all vulkan.h
// types so the header stays dependency-free for consumers.
class VulkanBackend final : public RenderBackend {
public:
    VulkanBackend();
    ~VulkanBackend() override;

    bool Init(const ContextConfig& config) override;
    void Shutdown() override;
    void Resize(int width, int height) override;

    void BeginFrame() override;
    void Clear(Color color) override;
    void EndFrame() override;

    void SetProjection(const Matrix& projection) override;
    void SetViewport(Rectangle viewport) override;
    void SetModelview(const Matrix& modelview) override;
    void SetScissor(bool enabled, int x, int y, int w, int h) override;
    void SetBlendMode(int mode) override;
    void SetDepthTest(bool enabled) override;
    void SetDepthMask(bool enabled) override;

    void DrawVertices(const Vertex* verts, std::size_t count,
                      DrawMode mode, unsigned int textureId) override;
    bool SupportsGpuSkinning() const override;
    void SetSkinning(const Matrix* matrices,int count) override;
    bool SupportsPBR() const override {return true;}
    void SetSurface(const Surface& surface) override;
    void DrawVerticesInstanced(const Vertex* verts,std::size_t count,unsigned int textureId,
                              const Matrix* transforms,int instances) override;

    unsigned int CreateTexture(const void* pixels, int width, int height,
                               PixelFormat format) override;
    bool SupportsTextureFormat(PixelFormat format) const override;
    void DestroyTexture(unsigned int textureId) override;
    void UpdateTexture(unsigned int textureId, int width, int height,
                       PixelFormat format, const void* pixels) override;
    int GenTextureMipmaps(unsigned int textureId) override;
    void SetTextureFilter(unsigned int textureId, int filter) override;
    void SetTextureWrap(unsigned int textureId, int wrap) override;
    Image ReadTexture(unsigned int textureId) override;
    unsigned int CreateCubemap(const void* rgbaFaces,int size) override;
    Image ReadCubemapFace(unsigned int texture,int face) override;
    Image ReadScreen() override;
    [[nodiscard]] unsigned int WhiteTexture() const override;

    unsigned int CreateFramebuffer(int width, int height,
                                   unsigned int* colorTexOut) override;
    void DestroyFramebuffer(unsigned int fboId, unsigned int colorTexId) override;
    void BindFramebuffer(unsigned int fboId, int width, int height) override;
    unsigned int CreateShaderProgram(const char* vsSrc, const char* fsSrc) override;
    void DestroyShaderProgram(unsigned int programId) override;
    int GetShaderUniformLocation(unsigned int programId, const char* name) override;
    void SetShaderUniform(unsigned int programId, int location,
                          const void* value, int uniformType, int count) override;
    void SetActiveShader(unsigned int programId) override;

    // Directional shadow mapping: a depth pass (shadow.vert/shadow.frag) writes
    // light-space depth into an R32F map that the lit pass samples with 3x3 PCF
    // at descriptor binding 9. This was long reported failing under MoltenVK
    // ("occluder fragments not stored"); the isolated rendering regression
    // (samples/vulkan_shadow_regression.cpp) traced it to a real bug in our own
    // shadow pipeline: the input-assembly topology was left zero-initialized,
    // selecting VK_PRIMITIVE_TOPOLOGY_POINT_LIST, so occluders rasterized as
    // vertex points. With TRIANGLE_LIST set, the offscreen depth store is proven
    // correct by host readback, so shadows are now advertised as supported.
    bool SupportsShadows() const override { return true; }
    void BeginShadowPass(const Matrix& lightViewProj, int resolution) override;
    void EndShadowPass() override;
    void ClearShadowMap() override;
    // Test/diagnostic hook: copy the R32F shadow map back to host and pack the
    // stored light-space depth into the R channel (0..255 = depth 0..1; 255 =
    // cleared/no occluder). Used by the isolated shadow rendering regression to
    // prove whether occluder fragments actually land in the offscreen shadow
    // target under the active driver (notably MoltenVK). Not part of the public
    // API; returns an empty Image if no shadow map exists.
    [[nodiscard]] Image ReadShadowMap();

    bool SupportsPersistentMesh() const override { return true; }
    unsigned int UploadMeshBuffer(const Vertex* verts, std::size_t count) override;
    void DestroyMeshBuffer(unsigned int handle) override;
    bool DrawMeshBuffer(unsigned int handle, std::size_t count,
                        unsigned int textureId, const Matrix* transforms, int instances) override;

    [[nodiscard]] const char* Name() const override;
    [[nodiscard]] BackendKind Kind() const override { return BackendKind::Vulkan; }

#if defined(MEOWY_WITH_IMGUI)
    bool ImGuiInit(void* glfwWindow) override;
    void ImGuiNewFrame() override;
    void ImGuiRender() override;
    void ImGuiShutdown() override;
#endif

private:
    unsigned int CreateTextureLayers(const void* pixels,int width,int height,PixelFormat format,int layers);
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace meowyrender::backend::vulkan
