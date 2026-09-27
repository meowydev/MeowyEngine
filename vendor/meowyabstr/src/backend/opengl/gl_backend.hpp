// meowyrender - src/backend/opengl/gl_backend.hpp  (internal)
#pragma once

#include "backend/render_backend.hpp"
#include <unordered_map>

namespace meowyrender::backend::gl {

// Fully-functional OpenGL 3.3 core backend. Renders the immediate-mode batch
// through a single VAO/VBO and a color+texture shader.
class GLBackend final : public RenderBackend {
public:
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
    bool SupportsGpuSkinning() const override { return activeShader_==0; }
    void SetSkinning(const Matrix* bones,int count) override;
    bool SupportsPBR() const override { return true; }
    void SetSurface(const Surface& surface) override { surface_=surface; }
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
    [[nodiscard]] unsigned int WhiteTexture() const override { return whiteTex_; }

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

    bool SupportsShadows() const override { return true; }
    void BeginShadowPass(const Matrix& lightViewProj, int resolution) override;
    void EndShadowPass() override;
    void ClearShadowMap() override { shadowMap_ = 0; cascadeCount_ = 0; }

    bool SupportsCascadedShadows() const override { return true; }
    void BeginShadowCascades(int count, int resolution) override;
    void BeginShadowCascade(int index, const Matrix& lightViewProj) override;
    void EndShadowCascade() override;
    void EndShadowCascades(const float* splitDepths, int count, const Matrix& viewMatrix) override;

    bool SupportsPersistentMesh() const override { return true; }
    unsigned int UploadMeshBuffer(const Vertex* verts, std::size_t count) override;
    void DestroyMeshBuffer(unsigned int handle) override;
    bool DrawMeshBuffer(unsigned int handle, std::size_t count,
                        unsigned int textureId, const Matrix* transforms,
                        int instances) override;

    [[nodiscard]] const char* Name() const override { return "OpenGL 3.3 Core"; }
    [[nodiscard]] BackendKind Kind() const override { return BackendKind::OpenGL; }

#if defined(MEOWY_WITH_IMGUI)
    bool ImGuiInit(void* glfwWindow) override;
    void ImGuiNewFrame() override;
    void ImGuiRender() override;
    void ImGuiShutdown() override;
#endif

private:
    void UploadMatrices();
    // Bind the default program uniforms + PBR surface + textures for a draw.
    void ApplyBuiltinDrawState(unsigned int prog, unsigned int textureId);
    // Configure a VAO's vertex + instance attribute layout for a vertex buffer.
    void ConfigureMeshVao(unsigned int vao, unsigned int vbo);

    GLFWwindow* window_ = nullptr;
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int instanceVbo_ = 0;
    unsigned int boneBuffer_ = 0, boneTexture_ = 0;
    std::vector<Matrix> bones_;
    Surface surface_;
    std::vector<Matrix> instances_;
    unsigned int whiteTex_ = 0;
    int locProjection_ = -1;
    int locModelview_ = -1;
    int locTexture_ = -1;
    Matrix projection_;
    Matrix modelview_;
    int fbWidth_ = 0;
    int fbHeight_ = 0;
    bool offscreen_ = false;
    std::unordered_map<unsigned int,int> cubemaps_;
    unsigned int activeShader_ = 0; // 0 = default batch program
    struct Framebuffer { unsigned int color=0, depth=0; };
    std::unordered_map<unsigned int, Framebuffer> framebuffers_;
    struct MeshBuffer { unsigned int vao=0, vbo=0; std::size_t count=0; };
    std::unordered_map<unsigned int, MeshBuffer> meshBuffers_;
    unsigned int nextMeshBuffer_ = 1;
    // Directional shadow map: a sampleable depth texture + its FBO.
    unsigned int shadowFbo_ = 0, shadowDepthTex_ = 0;
    int shadowResolution_ = 0;
    bool shadowPass_ = false;         // true while rendering the depth pass
    unsigned int shadowMap_ = 0;      // depth texture bound during lit draws
    Matrix shadowMatrix_{};           // light view*proj used for lit sampling
    // Cascaded shadow maps: up to 4 depth textures + matrices + split depths.
    static constexpr int kMaxCascades = 4;
    unsigned int cascadeFbo_[kMaxCascades] = {0,0,0,0};
    unsigned int cascadeTex_[kMaxCascades] = {0,0,0,0};
    Matrix cascadeMatrix_[kMaxCascades]{};
    float cascadeSplit_[kMaxCascades] = {0,0,0,0};
    Matrix cascadeViewMatrix_{};
    int cascadeCount_ = 0;            // >0 when cascaded shadows are active for lit draws
    int pendingCascadeCount_ = 0;     // cascades being recorded this set
    int cascadeResolution_ = 0;
    int savedViewport_[4] = {0,0,0,0};
};

} // namespace meowyrender::backend::gl
