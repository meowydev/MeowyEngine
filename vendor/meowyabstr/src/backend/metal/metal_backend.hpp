// meowyrender - src/backend/metal/metal_backend.hpp  (internal)
#pragma once

#include "backend/render_backend.hpp"

namespace meowyrender::backend::metal {

// Metal backend for macOS and visionOS. Implemented in metal_backend.mm as
// Objective-C++. The pimpl hides all Metal / QuartzCore types from C++ TUs.
class MetalBackend final : public RenderBackend {
public:
    MetalBackend();
    ~MetalBackend() override;

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
    void SetSkinning(const Matrix* bones,int count) override;
    bool SupportsPBR() const override { return true; }
    void SetSurface(const Surface& surface) override;
    void DrawVerticesInstanced(const Vertex* verts, std::size_t count,
                              unsigned int textureId, const Matrix* transforms, int instances) override;

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

    bool SupportsPersistentMesh() const override { return true; }
    unsigned int UploadMeshBuffer(const Vertex* verts, std::size_t count) override;
    void DestroyMeshBuffer(unsigned int handle) override;
    bool DrawMeshBuffer(unsigned int handle, std::size_t count,
                        unsigned int textureId, const Matrix* transforms,
                        int instances) override;

    bool SupportsShadows() const override { return true; }
    void BeginShadowPass(const Matrix& lightViewProj, int resolution) override;
    void EndShadowPass() override;
    void ClearShadowMap() override;

    bool SupportsCascadedShadows() const override { return true; }
    void BeginShadowCascades(int count, int resolution) override;
    void BeginShadowCascade(int index, const Matrix& lightViewProj) override;
    void EndShadowCascade() override;
    void EndShadowCascades(const float* splitDepths, int count, const Matrix& viewMatrix) override;

    [[nodiscard]] const char* Name() const override { return "Metal (MTLDevice)"; }
    [[nodiscard]] BackendKind Kind() const override { return BackendKind::Metal; }

#if defined(MEOWY_WITH_IMGUI)
    bool ImGuiInit(void* glfwWindow) override;
    void ImGuiNewFrame() override;
    void ImGuiRender() override;
    void ImGuiShutdown() override;
#endif

private:
    struct Impl;      // holds id<MTLDevice>, CAMetalLayer, pipeline, etc.
    Impl* impl_ = nullptr;
};

} // namespace meowyrender::backend::metal
