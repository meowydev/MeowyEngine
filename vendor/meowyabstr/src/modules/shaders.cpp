// meowyrender - src/modules/shaders.cpp
// Custom shaders + render textures (offscreen framebuffers).
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <fstream>
#include <sstream>
#include <cstdio>

namespace meowyrender {

using detail::State;

// ===========================================================================
// Shaders
// ===========================================================================
namespace {
std::string ReadFileText(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

Shader LoadShaderFromMemory(const std::string& vsCode, const std::string& fsCode) {
    Shader shader{};
    auto& s = State();
    if (!s.backend) return shader;
    shader.id = s.backend->CreateShaderProgram(vsCode.c_str(), fsCode.c_str());
    return shader;
}

Shader LoadShader(const std::string& vsFileName, const std::string& fsFileName) {
    const std::string vs = ReadFileText(vsFileName);
    const std::string fs = ReadFileText(fsFileName);
    if (vs.empty() || fs.empty()) {
        std::fprintf(stderr, "[meowyrender] LoadShader: failed to read shader files\n");
        return {};
    }
    return LoadShaderFromMemory(vs, fs);
}

bool IsShaderValid(Shader shader) { return shader.id != 0; }

int GetShaderLocation(Shader shader, const std::string& uniformName) {
    auto& s = State();
    if (!s.backend || shader.id == 0) return -1;
    return s.backend->GetShaderUniformLocation(shader.id, uniformName.c_str());
}

void SetShaderValueV(Shader shader, int locIndex, const void* value,
                     ShaderUniformType uniformType, int count) {
    auto& s = State();
    if (!s.backend || shader.id == 0 || locIndex < 0) return;
    detail::FlushBatch();
    s.backend->SetShaderUniform(shader.id, locIndex, value,
                                static_cast<int>(uniformType), count);
}

void SetShaderValue(Shader shader, int locIndex, const void* value,
                    ShaderUniformType uniformType) {
    SetShaderValueV(shader, locIndex, value, uniformType, 1);
}

void UnloadShader(Shader shader) {
    detail::FlushBatch();
    auto& s = State();
    if (s.backend && shader.id) s.backend->DestroyShaderProgram(shader.id);
}

void BeginShaderMode(Shader shader) {
    detail::FlushBatch();
    State().activeShader=shader.id;
    if (State().backend) State().backend->SetActiveShader(shader.id);
}

void EndShaderMode() {
    detail::FlushBatch();
    State().activeShader=0;
    if (State().backend) State().backend->SetActiveShader(0);
}

// ===========================================================================
// Render textures (offscreen framebuffers)
// ===========================================================================
RenderTexture2D LoadRenderTexture(int width, int height) {
    RenderTexture2D target{};
    auto& s = State();
    if (!s.backend) return target;
    unsigned int colorTex = 0;
    target.id = s.backend->CreateFramebuffer(width, height, &colorTex);
    target.texture.id = colorTex;
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = PixelFormat::Uncompressed_R8G8B8A8;
    return target;
}

void UnloadRenderTexture(RenderTexture2D target) {
    auto& s = State();
    if (s.backend) s.backend->DestroyFramebuffer(target.id, target.texture.id);
}

void BeginTextureMode(RenderTexture2D target) {
    auto& s = State();
    detail::FlushBatch();
    s.targetWidth=target.texture.width;s.targetHeight=target.texture.height;
    if (s.backend) s.backend->BindFramebuffer(target.id, target.texture.width,
                                              target.texture.height);
    // Render-texture space: origin top-left matching the target dimensions.
    s.projection = MatrixOrtho(0, target.texture.width, target.texture.height, 0, -1.0, 1.0);
    s.modelview = MatrixIdentity();
    if (s.backend) {
        s.backend->SetProjection(s.projection);
        s.backend->SetModelview(s.modelview);
    }
}

void EndTextureMode() {
    auto& s = State();
    detail::FlushBatch();
    s.targetWidth=s.screenWidth;s.targetHeight=s.screenHeight;
    // Rebind the default framebuffer (0) at window size.
    if (s.backend) s.backend->BindFramebuffer(0, s.screenWidth, s.screenHeight);
    s.projection = MatrixOrtho(0, s.screenWidth, s.screenHeight, 0, -1.0, 1.0);
    s.modelview = MatrixIdentity();
    if (s.backend) {
        s.backend->SetProjection(s.projection);
        s.backend->SetModelview(s.modelview);
    }
}

} // namespace meowyrender
