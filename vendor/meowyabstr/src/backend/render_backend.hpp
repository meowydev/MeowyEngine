// meowyrender - src/backend/render_backend.hpp  (internal)
// Abstract backend interface implemented by OpenGL / Metal / Vulkan.
//
// The public meowyrender API is a thin dispatch layer over this interface.
// All backends receive geometry as batched, backend-agnostic vertex data so
// the drawing modules (shapes/text/textures) never touch a graphics API.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "meowyrender/mr_types.hpp"
#include "meowyrender/mr_math.hpp"

struct GLFWwindow;

namespace meowyrender::backend {

// A single vertex in the immediate-mode batch. Position is in screen space
// (pixels, origin top-left); the backend applies the projection matrix.
struct Vertex {
    float x, y, z;       // position
    float u, v;          // texcoord
    std::uint8_t r, g, b, a; // color
    float joints[4]{};
    float weights[4]{};
    float nx=0,ny=0,nz=0;
};
struct Lighting {
    Vector3 eye{0,0,1};
    Vector3 direction{-0.5f,-1,-0.5f};
    Vector3 radiance{3,3,3};
    Vector3 ambient{0.03f,0.03f,0.03f};
    // Image-based lighting: an environment cubemap sampled for ambient diffuse
    // (along the surface normal) and specular reflection (along the reflection
    // vector). 0 disables IBL and falls back to the flat ambient term.
    unsigned int environment=0;
    float environmentIntensity=1.0f;
    int environmentMips=1; // mip count for roughness-based reflection blur
    // Precomputed IBL (optional, from GenEnvironmentLightMaps). When irradiance
    // != 0, the lit shader uses these instead of the runtime cubemap-sample
    // approximation: a diffuse irradiance cubemap, a roughness-prefiltered
    // specular cubemap (prefilterMips levels), and a BRDF integration LUT.
    unsigned int irradiance=0;
    unsigned int prefilter=0;
    int prefilterMips=1;
    unsigned int brdfLut=0;
    // Directional shadow mapping. When shadowMap != 0 the lit shader projects
    // fragments into light space with shadowMatrix and applies a shadow factor.
    unsigned int shadowMap=0;
    Matrix shadowMatrix{};
};
struct Surface {
    Lighting light;
    Vector3 emission{};
    float metallic=0,roughness=0.5f;
    unsigned int maps[5]{}; // metallic(B), roughness(G), normal, occlusion(R), emission
    unsigned int mask=0;
    bool enabled=false;
    // Alpha-mask cutoff (glTF MASK). Fragments with albedo alpha below this are
    // discarded. 0 disables the test (OPAQUE / BLEND).
    float alphaCutoff=0.0f;
};

// Draw primitive topology for a batch draw call.
enum class DrawMode { Triangles, Lines, Points };

// Which concrete backend an instance is. Mirrors the public meowyrender::Backend
// (minus Automatic). Kept internal so render_backend.hpp stays independent of
// the full public API header.
enum class BackendKind { OpenGL, Metal, Vulkan };

// Description passed to the backend at window/context creation time.
struct ContextConfig {
    int width = 0;
    int height = 0;
    unsigned int configFlags = 0;
    // The window is created by the platform layer (GLFW); the backend binds
    // its rendering context/surface to it. May be null for headless bring-up.
    void* window = nullptr;
};

// Backend interface. Methods are grouped by lifecycle.
class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    // --- lifecycle ---
    // Bind the graphics context/device/surface to the given window.
    virtual bool Init(const ContextConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual void Resize(int width, int height) = 0;

    // --- frame ---
    virtual void BeginFrame() = 0;
    virtual void Clear(Color color) = 0;
    // Present the completed frame (swap buffers / commit command buffer).
    virtual void EndFrame() = 0;

    // --- transform / render state ---
    // Set the 2D orthographic projection (called on resize / BeginMode2D).
    virtual void SetProjection(const Matrix& projection) = 0;
    virtual void SetModelview(const Matrix& modelview) = 0;
    virtual void SetScissor(bool enabled, int x, int y, int w, int h) = 0;
    virtual void SetBlendMode(int mode) = 0;
    // Enable/disable depth testing (used by 3D mode).
    virtual void SetDepthTest(bool enabled) = 0;
    // Independent depth-write control. Transparent (BLEND) geometry tests depth
    // but does not write it, so later transparent fragments are not occluded by
    // earlier ones drawn in front. Default (true) matches SetDepthTest coupling.
    virtual void SetDepthMask(bool /*enabled*/) {}
    // Normalized viewport within the current screen/offscreen target.
    virtual void SetViewport(Rectangle viewport) = 0;

    // --- immediate-mode batch ---
    // Submit a batch of vertices bound to a texture (0 = white/no texture).
    virtual void DrawVertices(const Vertex* verts, std::size_t count,
                              DrawMode mode, unsigned int textureId) = 0;
    virtual bool SupportsGpuSkinning() const { return false; }
    virtual void SetSkinning(const Matrix*, int) {}
    virtual bool SupportsPBR() const { return false; }
    virtual void SetSurface(const Surface&) {}

    // --- shadow mapping (optional) ---
    // Whether this backend implements directional shadow mapping.
    virtual bool SupportsShadows() const { return false; }
    // Begin rendering a depth-only pass from the light's point of view into an
    // internal sampleable depth texture. Geometry drawn until EndShadowPass is
    // captured as shadow occluders. lightViewProj is the light's view*proj.
    virtual void BeginShadowPass(const Matrix& /*lightViewProj*/, int /*resolution*/) {}
    virtual void EndShadowPass() {}
    // Stop sampling the shadow map on subsequent lit draws.
    virtual void ClearShadowMap() {}

    // --- cascaded shadow mapping (optional) ---
    // Whether this backend implements cascaded directional shadow maps.
    virtual bool SupportsCascadedShadows() const { return false; }
    // Configure `count` cascades (1..4) at the given resolution before rendering.
    virtual void BeginShadowCascades(int /*count*/, int /*resolution*/) {}
    // Begin the depth pass for cascade `index` with its light view-projection.
    virtual void BeginShadowCascade(int /*index*/, const Matrix& /*lightViewProj*/) {}
    virtual void EndShadowCascade() {}
    // Finish the cascade set; `splitDepths` are the view-space far distances of
    // each cascade (used by the lit shader to select a cascade per fragment).
    // `viewMatrix` is the scene camera's view matrix (for view-space depth).
    virtual void EndShadowCascades(const float* /*splitDepths*/, int /*count*/,
                                   const Matrix& /*viewMatrix*/) {}

    // --- persistent GPU mesh cache (optional) ---
    // Upload an interleaved Vertex array to a GPU-resident buffer and return a
    // handle (0 if unsupported/failed). The caller keeps the CPU copy for
    // bounds/skinning; this cache exists so meshes drawn every frame are not
    // re-streamed. Backends that do not override return 0 and callers fall back
    // to the immediate streaming path.
    virtual unsigned int UploadMeshBuffer(const Vertex*, std::size_t) { return 0; }
    virtual void DestroyMeshBuffer(unsigned int) {}
    // Draw a previously uploaded mesh buffer with per-draw transform(s). When
    // instances==1, transforms points at a single model matrix. Returns false
    // if the backend cannot service the request (caller falls back).
    virtual bool DrawMeshBuffer(unsigned int /*handle*/, std::size_t /*count*/,
                                unsigned int /*textureId*/, const Matrix* /*transforms*/,
                                int /*instances*/) { return false; }
    [[nodiscard]] virtual bool SupportsPersistentMesh() const { return false; }
    // Compatibility fallback for backends/custom shaders without an instance
    // transform input. Metal and OpenGL override the default shader path.
    virtual void DrawVerticesInstanced(const Vertex* verts, std::size_t count,
                                      unsigned int textureId, const Matrix* transforms, int instances) {
        std::vector<Vertex> transformed(verts,verts+count);
        for(int instance=0;instance<instances;++instance) {
            for(std::size_t i=0;i<count;++i) {
                Vector3 p=Vector3Transform({verts[i].x,verts[i].y,verts[i].z},transforms[instance]);
                transformed[i].x=p.x; transformed[i].y=p.y; transformed[i].z=p.z;
            }
            DrawVertices(transformed.data(),count,DrawMode::Triangles,textureId);
        }
    }

    // --- textures ---
    virtual bool SupportsTextureFormat(PixelFormat format) const { return format==PixelFormat::Uncompressed_R8G8B8A8; }
    // Upload pixel data, returning a backend texture handle (0 on failure).
    virtual unsigned int CreateTexture(const void* pixels, int width,
                                       int height, PixelFormat format) = 0;
    virtual void DestroyTexture(unsigned int textureId) = 0;
    // Replace the pixels of an existing texture (same dimensions/format).
    virtual void UpdateTexture(unsigned int textureId, int width, int height,
                               PixelFormat format, const void* pixels) = 0;
    // Generate mipmaps for a texture.
    virtual int GenTextureMipmaps(unsigned int textureId) = 0;
    // Set sampling filter (0=point,1=bilinear,2=trilinear).
    virtual void SetTextureFilter(unsigned int textureId, int filter) = 0;
    // Set wrap mode (0=repeat,1=clamp,2=mirror-repeat,3=mirror-clamp).
    virtual void SetTextureWrap(unsigned int textureId, int wrap) = 0;
    // Returns an owned, malloc-allocated RGBA8 image. Failure is an empty image.
    virtual Image ReadTexture(unsigned int) { return {}; }
    virtual unsigned int CreateCubemap(const void* rgbaFaces,int size) = 0;
    virtual Image ReadCubemapFace(unsigned int texture,int face) = 0;
    // Capture the active frame before EndFrame presents it.
    virtual Image ReadScreen() { return {}; }

    // A 1x1 white texture used for untextured (solid color) draws.
    [[nodiscard]] virtual unsigned int WhiteTexture() const = 0;

    // --- framebuffers (render textures) ---
    // Create an offscreen framebuffer with a color texture + depth buffer.
    // Returns the framebuffer handle; writes the color texture handle to
    // *colorTexOut. Returns 0 on failure.
    virtual unsigned int CreateFramebuffer(int width, int height,
                                           unsigned int* colorTexOut) = 0;
    virtual void DestroyFramebuffer(unsigned int fboId, unsigned int colorTexId) = 0;
    // Bind a framebuffer for rendering (0 = default/screen framebuffer).
    virtual void BindFramebuffer(unsigned int fboId, int width, int height) = 0;

    // --- shaders ---
    // Compile a shader program from vertex + fragment source. Returns program
    // handle (0 on failure).
    virtual unsigned int CreateShaderProgram(const char* vsSrc, const char* fsSrc) = 0;
    virtual void DestroyShaderProgram(unsigned int programId) = 0;
    // Look up a uniform location within a program.
    [[nodiscard]] virtual int GetShaderUniformLocation(unsigned int programId,
                                                       const char* name) = 0;
    // Set a uniform value. uniformType: 0=float,1=vec2,2=vec3,3=vec4,4=int.
    virtual void SetShaderUniform(unsigned int programId, int location,
                                  const void* value, int uniformType, int count) = 0;
    // Select the active shader program for subsequent draws (0 = default batch).
    virtual void SetActiveShader(unsigned int programId) = 0;

    // --- info ---
    [[nodiscard]] virtual const char* Name() const = 0;
    // Which concrete backend this instance is (for runtime reporting).
    [[nodiscard]] virtual BackendKind Kind() const = 0;

    // --- Dear ImGui integration (optional) ---------------------------------
    // Only meaningful when the library is built with MEOWY_WITH_IMGUI. The core
    // drives these from InitImGui / BeginDrawing / EndDrawing / ShutdownImGui:
    //   ImGuiInit   - set up the platform (imgui_impl_glfw) + this backend's
    //                 renderer backend (opengl3 / metal / ...). glfwWindow is the
    //                 GLFW window handle (void*, null on non-GLFW platforms).
    //                 Returns true if this backend supports ImGui and init
    //                 succeeded; false otherwise (caller reports unavailable).
    //   ImGuiNewFrame - begin a new ImGui frame (renderer + platform NewFrame +
    //                   ImGui::NewFrame). Called inside BeginDrawing.
    //   ImGuiRender   - ImGui::Render + render the draw data into this frame's
    //                   target/encoder. Called inside EndDrawing, before present.
    //   ImGuiShutdown - tear down this backend's ImGui renderer + platform.
    // Defaults are no-ops returning false, so backends without ImGui support
    // (and no-ImGui builds) degrade cleanly.
    virtual bool ImGuiInit(void* /*glfwWindow*/) { return false; }
    virtual void ImGuiNewFrame() {}
    virtual void ImGuiRender() {}
    virtual void ImGuiShutdown() {}
};

// --- Runtime backend factory ------------------------------------------------
// Whether `kind` was compiled into this build (ignores runtime availability).
[[nodiscard]] bool IsBackendCompiled(BackendKind kind);
// Construct the backend of `kind` if it is compiled in; nullptr otherwise. This
// only allocates the object (does not create a device/context); the caller runs
// Init(). Never throws or crashes for an uncompiled backend.
[[nodiscard]] std::unique_ptr<RenderBackend> CreateBackend(BackendKind kind);

} // namespace meowyrender::backend
