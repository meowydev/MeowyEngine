// meowyrender - src/backend/opengl/gl_backend.cpp  (internal)
//
// A self-contained OpenGL 3.3 core batch renderer. GL entry points are loaded
// at runtime via glfwGetProcAddress so we don't need a separate loader library.
#include "backend/opengl/gl_backend.hpp"
#include "core/mr_state.hpp"
#include "backend/pixel_conversion.hpp"
#include "backend/pbr_shader.hpp"
#include <string>

#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <algorithm>

// Pull GL types/constants. On macOS the system GL headers are deprecated but
// still present; we only use the 3.3 core subset and load functions manually.
#if defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION
#  include <OpenGL/gl3.h>
#else
#  include <GLFW/glfw3.h>  // brings in <GL/gl.h> via GLFW on many platforms
#endif

#include <GLFW/glfw3.h>

#if defined(MEOWY_WITH_IMGUI)
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#endif

namespace meowyrender::backend::gl {

namespace {

// --- Minimal runtime loader for the GL functions we use ------------------
// On macOS the core symbols are available directly from the framework, so we
// can call them without dlsym. On other platforms we resolve via GLFW.
#if !defined(__APPLE__)
// Function pointer typedefs (only what we need).
using PFNGLCREATESHADER = unsigned int (*)(unsigned int);
using PFNGLSHADERSOURCE = void (*)(unsigned int, int, const char* const*, const int*);
using PFNGLCOMPILESHADER = void (*)(unsigned int);
using PFNGLGETSHADERIV = void (*)(unsigned int, unsigned int, int*);
using PFNGLGETSHADERINFOLOG = void (*)(unsigned int, int, int*, char*);
using PFNGLCREATEPROGRAM = unsigned int (*)();
using PFNGLATTACHSHADER = void (*)(unsigned int, unsigned int);
using PFNGLLINKPROGRAM = void (*)(unsigned int);
using PFNGLGETPROGRAMIV = void (*)(unsigned int, unsigned int, int*);
using PFNGLUSEPROGRAM = void (*)(unsigned int);
using PFNGLDELETESHADER = void (*)(unsigned int);
using PFNGLGENVERTEXARRAYS = void (*)(int, unsigned int*);
using PFNGLDELETEVERTEXARRAYS = void (*)(int, const unsigned int*);
using PFNGLDELETEBUFFERS = void (*)(int, const unsigned int*);
using PFNGLBINDVERTEXARRAY = void (*)(unsigned int);
using PFNGLGENBUFFERS = void (*)(int, unsigned int*);
using PFNGLBINDBUFFER = void (*)(unsigned int, unsigned int);
using PFNGLBUFFERDATA = void (*)(unsigned int, std::ptrdiff_t, const void*, unsigned int);
using PFNGLVERTEXATTRIBPOINTER = void (*)(unsigned int, int, unsigned int, unsigned char, int, const void*);
using PFNGLENABLEVERTEXATTRIBARRAY = void (*)(unsigned int);
using PFNGLGETUNIFORMLOCATION = int (*)(unsigned int, const char*);
using PFNGLUNIFORMMATRIX4FV = void (*)(int, int, unsigned char, const float*);
using PFNGLUNIFORM1I = void (*)(int, int);
using PFNGLACTIVETEXTURE = void (*)(unsigned int);
using PFNGLGENERATEMIPMAP = void (*)(unsigned int);
using PFNGLGENFRAMEBUFFERS = void (*)(int, unsigned int*);
using PFNGLBINDFRAMEBUFFER = void (*)(unsigned int, unsigned int);
using PFNGLFRAMEBUFFERTEXTURE2D = void (*)(unsigned int, unsigned int, unsigned int, unsigned int, int);
using PFNGLGENRENDERBUFFERS = void (*)(int, unsigned int*);
using PFNGLBINDRENDERBUFFER = void (*)(unsigned int, unsigned int);
using PFNGLRENDERBUFFERSTORAGE = void (*)(unsigned int, unsigned int, int, int);
using PFNGLFRAMEBUFFERRENDERBUFFER = void (*)(unsigned int, unsigned int, unsigned int, unsigned int);
using PFNGLDELETEFRAMEBUFFERS = void (*)(int, const unsigned int*);
using PFNGLCHECKFRAMEBUFFERSTATUS = unsigned int (*)(unsigned int);
using PFNGLDELETERENDERBUFFERS = void (*)(int, const unsigned int*);
using PFNGLUNIFORM1F = void (*)(int, float);
using PFNGLUNIFORM1FV = void (*)(int,int,const float*);
using PFNGLUNIFORM2FV = void (*)(int, int, const float*);
using PFNGLUNIFORM3FV = void (*)(int, int, const float*);
using PFNGLUNIFORM4FV = void (*)(int, int, const float*);
using PFNGLUNIFORM1IV = void (*)(int, int, const int*);
using PFNGLDELETEPROGRAM = void (*)(unsigned int);
using PFNGLVERTEXATTRIBDIVISOR = void (*)(unsigned int, unsigned int);
using PFNGLDRAWARRAYSINSTANCED = void (*)(unsigned int, int, int, int);
using PFNGLCOMPRESSEDTEXIMAGE2D = void (*)(unsigned int,int,unsigned int,int,int,int,int,const void*);
using PFNGLTEXBUFFER = void (*)(unsigned int,unsigned int,unsigned int);
PFNGLTEXBUFFER glTexBuffer_;
using PFNGLCOMPRESSEDTEXSUBIMAGE2D = void (*)(unsigned int,int,int,int,int,int,unsigned int,int,const void*);
PFNGLCOMPRESSEDTEXIMAGE2D glCompressedTexImage2D_;
PFNGLCOMPRESSEDTEXSUBIMAGE2D glCompressedTexSubImage2D_;
PFNGLVERTEXATTRIBDIVISOR glVertexAttribDivisor_;
PFNGLDRAWARRAYSINSTANCED glDrawArraysInstanced_;

PFNGLCREATESHADER glCreateShader_;
PFNGLSHADERSOURCE glShaderSource_;
PFNGLCOMPILESHADER glCompileShader_;
PFNGLGETSHADERIV glGetShaderiv_;
PFNGLGETSHADERINFOLOG glGetShaderInfoLog_;
PFNGLCREATEPROGRAM glCreateProgram_;
PFNGLATTACHSHADER glAttachShader_;
PFNGLLINKPROGRAM glLinkProgram_;
PFNGLGETPROGRAMIV glGetProgramiv_;
PFNGLUSEPROGRAM glUseProgram_;
PFNGLDELETESHADER glDeleteShader_;
PFNGLGENVERTEXARRAYS glGenVertexArrays_;
PFNGLDELETEVERTEXARRAYS glDeleteVertexArrays_;
PFNGLDELETEBUFFERS glDeleteBuffers_;
PFNGLBINDVERTEXARRAY glBindVertexArray_;
PFNGLGENBUFFERS glGenBuffers_;
PFNGLBINDBUFFER glBindBuffer_;
PFNGLBUFFERDATA glBufferData_;
PFNGLVERTEXATTRIBPOINTER glVertexAttribPointer_;
PFNGLENABLEVERTEXATTRIBARRAY glEnableVertexAttribArray_;
PFNGLGETUNIFORMLOCATION glGetUniformLocation_;
PFNGLUNIFORMMATRIX4FV glUniformMatrix4fv_;
PFNGLUNIFORM1I glUniform1i_;
PFNGLACTIVETEXTURE glActiveTexture_;
PFNGLGENERATEMIPMAP glGenerateMipmap_;
PFNGLGENFRAMEBUFFERS glGenFramebuffers_;
PFNGLBINDFRAMEBUFFER glBindFramebuffer_;
PFNGLFRAMEBUFFERTEXTURE2D glFramebufferTexture2D_;
PFNGLGENRENDERBUFFERS glGenRenderbuffers_;
PFNGLBINDRENDERBUFFER glBindRenderbuffer_;
PFNGLRENDERBUFFERSTORAGE glRenderbufferStorage_;
PFNGLFRAMEBUFFERRENDERBUFFER glFramebufferRenderbuffer_;
PFNGLDELETEFRAMEBUFFERS glDeleteFramebuffers_;
PFNGLCHECKFRAMEBUFFERSTATUS glCheckFramebufferStatus_;
PFNGLDELETERENDERBUFFERS glDeleteRenderbuffers_;
PFNGLUNIFORM1F glUniform1f_;
PFNGLUNIFORM1FV glUniform1fv_;
PFNGLUNIFORM2FV glUniform2fv_;
PFNGLUNIFORM3FV glUniform3fv_;
PFNGLUNIFORM4FV glUniform4fv_;
PFNGLUNIFORM1IV glUniform1iv_;
PFNGLDELETEPROGRAM glDeleteProgram_;

bool LoadGL() {
    auto L = [](const char* n) { return glfwGetProcAddress(n); };
    glCreateShader_ = reinterpret_cast<PFNGLCREATESHADER>(L("glCreateShader"));
    glShaderSource_ = reinterpret_cast<PFNGLSHADERSOURCE>(L("glShaderSource"));
    glCompileShader_ = reinterpret_cast<PFNGLCOMPILESHADER>(L("glCompileShader"));
    glGetShaderiv_ = reinterpret_cast<PFNGLGETSHADERIV>(L("glGetShaderiv"));
    glGetShaderInfoLog_ = reinterpret_cast<PFNGLGETSHADERINFOLOG>(L("glGetShaderInfoLog"));
    glCreateProgram_ = reinterpret_cast<PFNGLCREATEPROGRAM>(L("glCreateProgram"));
    glAttachShader_ = reinterpret_cast<PFNGLATTACHSHADER>(L("glAttachShader"));
    glLinkProgram_ = reinterpret_cast<PFNGLLINKPROGRAM>(L("glLinkProgram"));
    glGetProgramiv_ = reinterpret_cast<PFNGLGETPROGRAMIV>(L("glGetProgramiv"));
    glUseProgram_ = reinterpret_cast<PFNGLUSEPROGRAM>(L("glUseProgram"));
    glDeleteShader_ = reinterpret_cast<PFNGLDELETESHADER>(L("glDeleteShader"));
    glGenVertexArrays_ = reinterpret_cast<PFNGLGENVERTEXARRAYS>(L("glGenVertexArrays"));
    glDeleteVertexArrays_ = reinterpret_cast<PFNGLDELETEVERTEXARRAYS>(L("glDeleteVertexArrays"));
    glDeleteBuffers_ = reinterpret_cast<PFNGLDELETEBUFFERS>(L("glDeleteBuffers"));
    glBindVertexArray_ = reinterpret_cast<PFNGLBINDVERTEXARRAY>(L("glBindVertexArray"));
    glGenBuffers_ = reinterpret_cast<PFNGLGENBUFFERS>(L("glGenBuffers"));
    glBindBuffer_ = reinterpret_cast<PFNGLBINDBUFFER>(L("glBindBuffer"));
    glBufferData_ = reinterpret_cast<PFNGLBUFFERDATA>(L("glBufferData"));
    glVertexAttribPointer_ = reinterpret_cast<PFNGLVERTEXATTRIBPOINTER>(L("glVertexAttribPointer"));
    glEnableVertexAttribArray_ = reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAY>(L("glEnableVertexAttribArray"));
    glGetUniformLocation_ = reinterpret_cast<PFNGLGETUNIFORMLOCATION>(L("glGetUniformLocation"));
    glUniformMatrix4fv_ = reinterpret_cast<PFNGLUNIFORMMATRIX4FV>(L("glUniformMatrix4fv"));
    glUniform1i_ = reinterpret_cast<PFNGLUNIFORM1I>(L("glUniform1i"));
    glActiveTexture_ = reinterpret_cast<PFNGLACTIVETEXTURE>(L("glActiveTexture"));
    glTexBuffer_=reinterpret_cast<PFNGLTEXBUFFER>(L("glTexBuffer"));
    glCompressedTexImage2D_=reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2D>(L("glCompressedTexImage2D"));
    glCompressedTexSubImage2D_=reinterpret_cast<PFNGLCOMPRESSEDTEXSUBIMAGE2D>(L("glCompressedTexSubImage2D"));
    glVertexAttribDivisor_ = reinterpret_cast<PFNGLVERTEXATTRIBDIVISOR>(L("glVertexAttribDivisor"));
    glDrawArraysInstanced_ = reinterpret_cast<PFNGLDRAWARRAYSINSTANCED>(L("glDrawArraysInstanced"));
    glGenerateMipmap_ = reinterpret_cast<PFNGLGENERATEMIPMAP>(L("glGenerateMipmap"));
    glGenFramebuffers_ = reinterpret_cast<PFNGLGENFRAMEBUFFERS>(L("glGenFramebuffers"));
    glBindFramebuffer_ = reinterpret_cast<PFNGLBINDFRAMEBUFFER>(L("glBindFramebuffer"));
    glFramebufferTexture2D_ = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2D>(L("glFramebufferTexture2D"));
    glGenRenderbuffers_ = reinterpret_cast<PFNGLGENRENDERBUFFERS>(L("glGenRenderbuffers"));
    glBindRenderbuffer_ = reinterpret_cast<PFNGLBINDRENDERBUFFER>(L("glBindRenderbuffer"));
    glRenderbufferStorage_ = reinterpret_cast<PFNGLRENDERBUFFERSTORAGE>(L("glRenderbufferStorage"));
    glFramebufferRenderbuffer_ = reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFER>(L("glFramebufferRenderbuffer"));
    glDeleteFramebuffers_ = reinterpret_cast<PFNGLDELETEFRAMEBUFFERS>(L("glDeleteFramebuffers"));
    glCheckFramebufferStatus_ = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUS>(L("glCheckFramebufferStatus"));
    glDeleteRenderbuffers_ = reinterpret_cast<PFNGLDELETERENDERBUFFERS>(L("glDeleteRenderbuffers"));
    glUniform1f_ = reinterpret_cast<PFNGLUNIFORM1F>(L("glUniform1f"));
    glUniform1fv_ = reinterpret_cast<PFNGLUNIFORM1FV>(L("glUniform1fv"));
    glUniform2fv_ = reinterpret_cast<PFNGLUNIFORM2FV>(L("glUniform2fv"));
    glUniform3fv_ = reinterpret_cast<PFNGLUNIFORM3FV>(L("glUniform3fv"));
    glUniform4fv_ = reinterpret_cast<PFNGLUNIFORM4FV>(L("glUniform4fv"));
    glUniform1iv_ = reinterpret_cast<PFNGLUNIFORM1IV>(L("glUniform1iv"));
    glDeleteProgram_ = reinterpret_cast<PFNGLDELETEPROGRAM>(L("glDeleteProgram"));
    return glCreateShader_ && glCreateProgram_ && glGenVertexArrays_;
}
// Map the wrappers onto the raylib-ish names used below.
#  define glCreateShader glCreateShader_
#  define glShaderSource glShaderSource_
#  define glCompileShader glCompileShader_
#  define glGetShaderiv glGetShaderiv_
#  define glGetShaderInfoLog glGetShaderInfoLog_
#  define glCreateProgram glCreateProgram_
#  define glAttachShader glAttachShader_
#  define glLinkProgram glLinkProgram_
#  define glGetProgramiv glGetProgramiv_
#  define glUseProgram glUseProgram_
#  define glDeleteShader glDeleteShader_
#  define glGenVertexArrays glGenVertexArrays_
#  define glDeleteVertexArrays glDeleteVertexArrays_
#  define glDeleteBuffers glDeleteBuffers_
#  define glBindVertexArray glBindVertexArray_
#  define glGenBuffers glGenBuffers_
#  define glBindBuffer glBindBuffer_
#  define glBufferData glBufferData_
#  define glVertexAttribPointer glVertexAttribPointer_
#  define glEnableVertexAttribArray glEnableVertexAttribArray_
#  define glGetUniformLocation glGetUniformLocation_
#  define glUniformMatrix4fv glUniformMatrix4fv_
#  define glUniform1i glUniform1i_
#  define glActiveTexture glActiveTexture_
#  define glTexBuffer glTexBuffer_
#  define glCompressedTexImage2D glCompressedTexImage2D_
#  define glCompressedTexSubImage2D glCompressedTexSubImage2D_
#  define glVertexAttribDivisor glVertexAttribDivisor_
#  define glDrawArraysInstanced glDrawArraysInstanced_
#  define glGenerateMipmap glGenerateMipmap_
#  define glGenFramebuffers glGenFramebuffers_
#  define glBindFramebuffer glBindFramebuffer_
#  define glFramebufferTexture2D glFramebufferTexture2D_
#  define glGenRenderbuffers glGenRenderbuffers_
#  define glBindRenderbuffer glBindRenderbuffer_
#  define glRenderbufferStorage glRenderbufferStorage_
#  define glFramebufferRenderbuffer glFramebufferRenderbuffer_
#  define glDeleteFramebuffers glDeleteFramebuffers_
#  define glCheckFramebufferStatus glCheckFramebufferStatus_
#  define glDeleteRenderbuffers glDeleteRenderbuffers_
#  define glUniform1f glUniform1f_
#  define glUniform1fv glUniform1fv_
#  define glUniform2fv glUniform2fv_
#  define glUniform3fv glUniform3fv_
#  define glUniform4fv glUniform4fv_
#  define glUniform1iv glUniform1iv_
#  define glDeleteProgram glDeleteProgram_
#else
bool LoadGL() { return true; } // macOS core symbols link directly
#endif

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTex;
layout(location = 2) in vec4 aColor;
layout(location = 3) in mat4 aInstance;
layout(location = 7) in vec4 aJoints;
layout(location = 8) in vec4 aWeights;
layout(location = 9) in vec3 aNormal;
uniform samplerBuffer uBones;
uniform mat4 uProjection;
uniform mat4 uModelview;
out vec2 vTex;
out vec4 vColor;
out vec3 vWorld;
out vec3 vNormal;
vec3 transformNormal(mat4 matrix,vec3 normal) {
    mat3 basis=mat3(matrix);
    return abs(determinant(basis))>0.000001?transpose(inverse(basis))*normal:normal;
}
void main() {
    vec4 position=vec4(aPos,1.0);
    vec3 normal=aNormal;
    float sum=dot(aWeights,vec4(1));
    if(sum>0.0) {
        position=vec4(0);
        normal=vec3(0);
        for(int i=0;i<4;++i) if(aWeights[i]>0.0) {
            int bone=int(aJoints[i])*4;
            mat4 skin=mat4(texelFetch(uBones,bone),texelFetch(uBones,bone+1),texelFetch(uBones,bone+2),texelFetch(uBones,bone+3));
            position+=skin*vec4(aPos,1.0)*(aWeights[i]/sum);
            normal+=transformNormal(skin,aNormal)*(aWeights[i]/sum);
        }
    }
    vec4 world=aInstance*position;
    vWorld=world.xyz;
    vNormal=transformNormal(aInstance,normal);
    gl_Position = uProjection * uModelview * world;
    vTex = aTex;
    vColor = aColor;
}
)";

const std::string kFragmentSource = std::string(R"(#version 330 core
in vec2 vTex;
in vec4 vColor;
in vec3 vWorld;
in vec3 vNormal;
uniform sampler2D uTexture;
uniform sampler2D uMetalMap,uRoughMap,uNormalMap,uAoMap,uEmissionMap;
uniform samplerCube uEnvironment;
// Precomputed split-sum IBL maps.
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D uBrdfLut;
uniform int uHasPrecomputed,uPrefilterMips;
uniform sampler2D uShadowMap;
uniform mat4 uShadowMatrix;
uniform vec3 uEye,uDirection,uRadiance,uAmbient,uEmission;
uniform float uMetallic,uRoughness;
uniform float uAlphaCutoff;
uniform float uEnvIntensity;
uniform int uLit,uMaps,uEnvMips,uHasEnv,uHasShadow;
// Cascaded shadow maps (up to 4). uCascadeCount==0 falls back to the single map.
uniform sampler2D uCascadeMap0,uCascadeMap1,uCascadeMap2,uCascadeMap3;
uniform mat4 uCascadeMatrix[4];
uniform float uCascadeSplit[4];
uniform mat4 uViewMatrix;
uniform int uCascadeCount;
out vec4 FragColor;
// PCF sample against a specific cascade map.
float sampleCascade(int idx,vec3 proj,float bias) {
    vec2 texel; float sum=0.0;
    // GLSL requires constant sampler indexing; branch per cascade.
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y) {
        vec2 uv=proj.xy+vec2(x,y)*(1.0/2048.0);
        float d= idx==0?texture(uCascadeMap0,uv).r:
                 idx==1?texture(uCascadeMap1,uv).r:
                 idx==2?texture(uCascadeMap2,uv).r:texture(uCascadeMap3,uv).r;
        sum+=proj.z-bias>d?0.0:1.0;
    }
    return sum/9.0;
}
// 3x3 PCF directional shadow factor (1 = lit, 0 = fully shadowed).
float shadowFactor(vec3 world,vec3 n,vec3 l) {
    float bias=max(0.0025*(1.0-dot(n,l)),0.0005);
    if(uCascadeCount>0) {
        // Select the cascade by the fragment's view-space depth.
        float viewDepth=-(uViewMatrix*vec4(world,1.0)).z;
        int idx=uCascadeCount-1;
        for(int c=0;c<uCascadeCount;++c) if(viewDepth<=uCascadeSplit[c]) { idx=c; break; }
        vec4 lightSpace=uCascadeMatrix[idx]*vec4(world,1.0);
        vec3 proj=lightSpace.xyz/lightSpace.w*0.5+0.5;
        if(proj.z>1.0||proj.x<0.0||proj.x>1.0||proj.y<0.0||proj.y>1.0) return 1.0;
        return sampleCascade(idx,proj,bias);
    }
    if(uHasShadow==0) return 1.0;
    vec4 lightSpace=uShadowMatrix*vec4(world,1.0);
    vec3 proj=lightSpace.xyz/lightSpace.w*0.5+0.5;
    if(proj.z>1.0||proj.x<0.0||proj.x>1.0||proj.y<0.0||proj.y>1.0) return 1.0;
    vec2 texel=1.0/vec2(textureSize(uShadowMap,0));
    float sum=0.0;
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y) {
        float d=texture(uShadowMap,proj.xy+vec2(x,y)*texel).r;
        sum+=proj.z-bias>d?0.0:1.0;
    }
    return sum/9.0;
}
)" ) + PbrFunctions + R"(
void main() {
    vec4 color=texture(uTexture,vTex)*vColor;
    // glTF alpha MASK: discard fragments below the cutoff.
    if(uAlphaCutoff>0.0 && color.a<uAlphaCutoff) discard;
    if(uLit==0) {FragColor=color; return;}
    vec3 normal=dot(vNormal,vNormal)>0.000001?safeNormal(vNormal):safeNormal(cross(dFdx(vWorld),dFdy(vWorld)));
    if((uMaps&4)!=0) {
        vec3 tangentNormal=texture(uNormalMap,vTex).xyz*2.0-1.0;
        vec3 px=dFdx(vWorld),py=dFdy(vWorld); vec2 tx=dFdx(vTex),ty=dFdy(vTex);
        float determinant=tx.x*ty.y-tx.y*ty.x;
        if(abs(determinant)>0.000001) {
            vec3 tangent=safeNormal((px*ty.y-py*tx.y)/determinant);
            vec3 bitangent=safeNormal((py*tx.x-px*ty.x)/determinant);
            normal=safeNormal(tangent*tangentNormal.x+bitangent*tangentNormal.y+normal*tangentNormal.z);
        }
    }
    float metallic=uMetallic*((uMaps&1)!=0?texture(uMetalMap,vTex).b:1.0);
    float roughness=uRoughness*((uMaps&2)!=0?texture(uRoughMap,vTex).g:1.0);
    float ao=(uMaps&8)!=0?texture(uAoMap,vTex).r:1.0;
    vec3 emission=uEmission*((uMaps&16)!=0?pow(texture(uEmissionMap,vTex).rgb,vec3(2.2)):vec3(1));
    vec3 baseLinear=pow(max(color.rgb,vec3(0)),vec3(2.2));
    // Shadowing attenuates the direct directional term only (not ambient/IBL).
    float shadow=shadowFactor(vWorld,normal,safeNormal(-uDirection));
    vec3 directRadiance=uRadiance*shadow;
    if(uHasPrecomputed!=0) {
        // Precomputed split-sum IBL: irradiance cubemap (diffuse), prefiltered
        // cubemap (specular via roughness LOD) and the BRDF integration LUT.
        vec3 view=safeNormal(uEye-vWorld);
        vec3 refl=reflect(-view,normal);
        vec3 irradiance=pow(texture(uIrradiance,normal).rgb,vec3(2.2))*uEnvIntensity;
        float lod=roughness*float(max(uPrefilterMips-1,0));
        vec3 prefiltered=pow(textureLod(uPrefilter,refl,lod).rgb,vec3(2.2))*uEnvIntensity;
        vec2 brdf=texture(uBrdfLut,vec2(max(dot(normal,view),0.0),roughness)).rg;
        FragColor=vec4(shadePBRSplitSum(baseLinear,normal,vWorld,uEye,uDirection,directRadiance,emission,metallic,roughness,ao,irradiance,prefiltered,brdf),color.a);
        return;
    }
    if(uHasEnv!=0) {
        vec3 view=safeNormal(uEye-vWorld);
        vec3 refl=reflect(-view,normal);
        // Sample the environment for diffuse (normal) and specular (reflection)
        // ambient. Roughness selects a blurred mip of the reflection.
        vec3 envDiffuse=pow(texture(uEnvironment,normal).rgb,vec3(2.2))*uEnvIntensity;
        float lod=roughness*float(max(uEnvMips-1,0));
        vec3 envSpecular=pow((uEnvMips>1?textureLod(uEnvironment,refl,lod):texture(uEnvironment,refl)).rgb,vec3(2.2))*uEnvIntensity;
        FragColor=vec4(shadePBRIBL(baseLinear,normal,vWorld,uEye,uDirection,directRadiance,emission,metallic,roughness,ao,envDiffuse,envSpecular),color.a);
        return;
    }
    FragColor=vec4(shadePBR(baseLinear,normal,vWorld,uEye,uDirection,directRadiance,uAmbient,emission,metallic,roughness,ao),color.a);
}
)";
const char* kFragmentShader=kFragmentSource.c_str();

unsigned int CompileShader(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[meowyrender][GL] shader compile error: %s\n", log);
    }
    return shader;
}

unsigned int GLModeOf(DrawMode m) {
    switch (m) {
        case DrawMode::Triangles: return GL_TRIANGLES;
        case DrawMode::Lines: return GL_LINES;
        case DrawMode::Points: return GL_POINTS;
    }
    return GL_TRIANGLES;
}

} // namespace

bool GLBackend::Init(const ContextConfig& config) {
    window_ = static_cast<GLFWwindow*>(config.window);
    fbWidth_ = config.width;
    fbHeight_ = config.height;

    glfwMakeContextCurrent(window_);
    if (!LoadGL()) {
        std::fprintf(stderr, "[meowyrender][GL] failed to load GL functions\n");
        return false;
    }

    // Build the batch shader program.
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    { int lk=0; glGetProgramiv(program_,GL_LINK_STATUS,&lk); if(!lk){char log[2048];glGetProgramInfoLog(program_,sizeof(log),nullptr,log);std::fprintf(stderr,"[meowyrender][GL] program link error: %s\n",log);} }
    glDeleteShader(vs);
    glDeleteShader(fs);

    locProjection_ = glGetUniformLocation(program_, "uProjection");
    locModelview_ = glGetUniformLocation(program_, "uModelview");
    locTexture_ = glGetUniformLocation(program_, "uTexture");

    // VAO/VBO for the dynamic vertex batch.
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &instanceVbo_);
    glGenBuffers(1,&boneBuffer_); glGenTextures(1,&boneTexture_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    const int stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, r)));
    glEnableVertexAttribArray(7); glVertexAttribPointer(7,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,joints)));
    glEnableVertexAttribArray(8); glVertexAttribPointer(8,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,weights)));
    glEnableVertexAttribArray(9); glVertexAttribPointer(9,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,nx)));

    // 1x1 white texture for untextured solid draws.
    const unsigned char white[4] = {255, 255, 255, 255};
    whiteTex_ = CreateTexture(white, 1, 1, PixelFormat::Uncompressed_R8G8B8A8);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    projection_ = MatrixIdentity();
    modelview_ = MatrixIdentity();
    return true;
}

void GLBackend::Shutdown() {
    while(!framebuffers_.empty()) DestroyFramebuffer(framebuffers_.begin()->first,0);
    if (whiteTex_) DestroyTexture(whiteTex_);
    // GL context torn down with the window by the platform layer.
    window_ = nullptr;
}

void GLBackend::Resize(int width, int height) {
    fbWidth_ = width;
    fbHeight_ = height;
    glViewport(0, 0, width, height);
}

void GLBackend::BeginFrame() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    fbWidth_ = w; fbHeight_ = h;
    glViewport(0, 0, w, h);
    glUseProgram(program_);
}

void GLBackend::Clear(Color color) {
    glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                 color.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLBackend::EndFrame() {
    glfwSwapBuffers(window_);
}

void GLBackend::UploadMatrices() {
    glUniformMatrix4fv(locProjection_, 1, GL_FALSE, &projection_.m0);
    glUniformMatrix4fv(locModelview_, 1, GL_FALSE, &modelview_.m0);
}

void GLBackend::SetProjection(const Matrix& p) { projection_ = p; }
void GLBackend::SetViewport(Rectangle v) {
    glViewport(static_cast<int>(v.x*fbWidth_),static_cast<int>((offscreen_?v.y:1-v.y-v.height)*fbHeight_),
               static_cast<int>(v.width*fbWidth_),static_cast<int>(v.height*fbHeight_));
}
void GLBackend::SetModelview(const Matrix& m) { modelview_ = m; }

void GLBackend::SetScissor(bool enabled, int x, int y, int w, int h) {
    if (enabled) {
        glEnable(GL_SCISSOR_TEST);
        const auto& state=detail::State();
        const double sx=offscreen_?1.0:static_cast<double>(fbWidth_)/std::max(1,state.screenWidth);
        const double sy=offscreen_?1.0:static_cast<double>(fbHeight_)/std::max(1,state.screenHeight);
        const auto left=std::clamp<long long>(static_cast<long long>(x*sx),0,fbWidth_);
        const auto top=std::clamp<long long>(static_cast<long long>(y*sy),0,fbHeight_);
        const auto right=std::clamp<long long>(static_cast<long long>((static_cast<double>(x)+w)*sx),left,fbWidth_);
        const auto bottom=std::clamp<long long>(static_cast<long long>((static_cast<double>(y)+h)*sy),top,fbHeight_);
        glScissor(static_cast<int>(left),offscreen_?static_cast<int>(top):fbHeight_-static_cast<int>(bottom),static_cast<int>(right-left),static_cast<int>(bottom-top));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void GLBackend::SetBlendMode(int mode) {
    switch (mode) {
        case 1: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;            // additive
        case 2: glBlendFunc(GL_DST_COLOR, GL_ZERO); break;           // multiply
        default: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // alpha
    }
}

void GLBackend::SetDepthTest(bool enabled) {
    if (enabled) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void GLBackend::SetDepthMask(bool enabled) {
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void GLBackend::DrawVertices(const Vertex* verts, std::size_t count,
                             DrawMode mode, unsigned int textureId) {
    if (count == 0) return;
    const unsigned int prog = activeShader_ ? activeShader_ : program_;
    ApplyBuiltinDrawState(prog, textureId);

    // Stream the vertices + current instance transforms through the shared VAO.
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER,instanceVbo_);
    const Matrix identity=MatrixTranspose(MatrixIdentity());
    glBufferData(GL_ARRAY_BUFFER,static_cast<std::ptrdiff_t>(instances_.empty()?sizeof(Matrix):instances_.size()*sizeof(Matrix)),instances_.empty()?&identity:instances_.data(),GL_DYNAMIC_DRAW);
    for(unsigned int column=0;column<4;++column) {
        glEnableVertexAttribArray(3+column);
        glVertexAttribPointer(3+column,4,GL_FLOAT,GL_FALSE,sizeof(Matrix),reinterpret_cast<void*>(column*4*sizeof(float)));
        glVertexAttribDivisor(3+column,1);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<std::ptrdiff_t>(count * sizeof(Vertex)),
                 verts, GL_DYNAMIC_DRAW);
    glDrawArraysInstanced(GLModeOf(mode),0,static_cast<int>(count),instances_.empty()?1:static_cast<int>(instances_.size()));
}

// Bind the default program's uniforms, PBR surface, textures and bone palette.
// Custom shaders skip the built-in uniform block.
void GLBackend::ApplyBuiltinDrawState(unsigned int prog, unsigned int textureId) {
    glUseProgram(prog);
    const int locP = glGetUniformLocation(prog, "uProjection");
    const int locM = glGetUniformLocation(prog, "uModelview");
    const int locT = glGetUniformLocation(prog, "uTexture");
    // Store offscreen row zero at the logical top, like loaded images and the
    // other backends. This also makes readback independent of texture origin.
    Matrix projection=projection_;
    if(offscreen_) {projection.m1=-projection.m1;projection.m5=-projection.m5;projection.m9=-projection.m9;projection.m13=-projection.m13;}
    if (locP >= 0) glUniformMatrix4fv(locP, 1, GL_TRUE, &projection.m0);
    if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_TRUE, &modelview_.m0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(cubemaps_.contains(textureId)?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D, textureId ? textureId : whiteTex_);
    if (locT >= 0) glUniform1i(locT, 0);
    if(!activeShader_) {
        glUniform1i(glGetUniformLocation(prog,"uLit"),surface_.enabled?1:0);
        glUniform1i(glGetUniformLocation(prog,"uMaps"),static_cast<int>(surface_.mask));
        glUniform1f(glGetUniformLocation(prog,"uMetallic"),surface_.metallic);
        glUniform1f(glGetUniformLocation(prog,"uRoughness"),surface_.roughness);
        glUniform1f(glGetUniformLocation(prog,"uAlphaCutoff"),surface_.alphaCutoff);
        // Image-based lighting environment cubemap on texture unit 7.
        const bool hasEnv=surface_.light.environment!=0;
        glUniform1i(glGetUniformLocation(prog,"uHasEnv"),hasEnv?1:0);
        glUniform1f(glGetUniformLocation(prog,"uEnvIntensity"),surface_.light.environmentIntensity);
        glUniform1i(glGetUniformLocation(prog,"uEnvMips"),surface_.light.environmentMips);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_CUBE_MAP,hasEnv?surface_.light.environment:0);
        glUniform1i(glGetUniformLocation(prog,"uEnvironment"),7);
        // Precomputed split-sum IBL maps on units 13/14/15. These sampler
        // uniforms must ALWAYS point at their own units and have a texture of
        // the matching type bound there: leaving a samplerCube defaulted to
        // unit 0 (which holds a sampler2D) makes the draw call invalid and
        // renders black, even for unlit 2D. Bind them unconditionally.
        const bool hasPrecomputed=surface_.light.irradiance!=0 && surface_.light.prefilter!=0 && surface_.light.brdfLut!=0;
        glUniform1i(glGetUniformLocation(prog,"uHasPrecomputed"),hasPrecomputed?1:0);
        glUniform1i(glGetUniformLocation(prog,"uPrefilterMips"),hasPrecomputed?surface_.light.prefilterMips:1);
        glActiveTexture(GL_TEXTURE13); glBindTexture(GL_TEXTURE_CUBE_MAP,hasPrecomputed?surface_.light.irradiance:0);
        glUniform1i(glGetUniformLocation(prog,"uIrradiance"),13);
        glActiveTexture(GL_TEXTURE14); glBindTexture(GL_TEXTURE_CUBE_MAP,hasPrecomputed?surface_.light.prefilter:0);
        glUniform1i(glGetUniformLocation(prog,"uPrefilter"),14);
        glActiveTexture(GL_TEXTURE15); glBindTexture(GL_TEXTURE_2D,hasPrecomputed?surface_.light.brdfLut:0);
        glUniform1i(glGetUniformLocation(prog,"uBrdfLut"),15);
        // Directional shadow map on texture unit 8 (only outside the depth pass).
        const bool hasShadow = shadowMap_!=0 && !shadowPass_;
        glUniform1i(glGetUniformLocation(prog,"uHasShadow"),hasShadow?1:0);
        if(hasShadow) {
            glUniformMatrix4fv(glGetUniformLocation(prog,"uShadowMatrix"),1,GL_TRUE,&shadowMatrix_.m0);
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D,shadowMap_);
            glUniform1i(glGetUniformLocation(prog,"uShadowMap"),8);
        }
        // Cascaded shadow maps on texture units 9..12 (outside the depth pass).
        const int cascadeCount = (cascadeCount_>0 && !shadowPass_) ? cascadeCount_ : 0;
        glUniform1i(glGetUniformLocation(prog,"uCascadeCount"),cascadeCount);
        if(cascadeCount>0) {
            glUniformMatrix4fv(glGetUniformLocation(prog,"uViewMatrix"),1,GL_TRUE,&cascadeViewMatrix_.m0);
            glUniformMatrix4fv(glGetUniformLocation(prog,"uCascadeMatrix"),cascadeCount,GL_TRUE,&cascadeMatrix_[0].m0);
            glUniform1fv(glGetUniformLocation(prog,"uCascadeSplit"),cascadeCount,cascadeSplit_);
            const char* names[4]={"uCascadeMap0","uCascadeMap1","uCascadeMap2","uCascadeMap3"};
            for(int i=0;i<kMaxCascades;++i) {
                glActiveTexture(GL_TEXTURE9+i);
                glBindTexture(GL_TEXTURE_2D, i<cascadeCount ? cascadeTex_[i] : (cascadeTex_[0]?cascadeTex_[0]:whiteTex_));
                glUniform1i(glGetUniformLocation(prog,names[i]),9+i);
            }
        }
        glActiveTexture(GL_TEXTURE0);
        glUniform3fv(glGetUniformLocation(prog,"uEye"),1,&surface_.light.eye.x);
        glUniform3fv(glGetUniformLocation(prog,"uDirection"),1,&surface_.light.direction.x);
        glUniform3fv(glGetUniformLocation(prog,"uRadiance"),1,&surface_.light.radiance.x);
        glUniform3fv(glGetUniformLocation(prog,"uAmbient"),1,&surface_.light.ambient.x);
        glUniform3fv(glGetUniformLocation(prog,"uEmission"),1,&surface_.emission.x);
        const char* names[]={"uMetalMap","uRoughMap","uNormalMap","uAoMap","uEmissionMap"};
        for(int i=0;i<5;++i) {
            glActiveTexture(GL_TEXTURE2+i); glBindTexture(GL_TEXTURE_2D,surface_.maps[i]?surface_.maps[i]:whiteTex_);
            glUniform1i(glGetUniformLocation(prog,names[i]),i+2);
        }
        Matrix identity=MatrixTranspose(MatrixIdentity());
        glBindBuffer(GL_TEXTURE_BUFFER,boneBuffer_);
        glBufferData(GL_TEXTURE_BUFFER,static_cast<std::ptrdiff_t>(bones_.empty()?sizeof(Matrix):bones_.size()*sizeof(Matrix)),bones_.empty()?&identity:bones_.data(),GL_DYNAMIC_DRAW);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_BUFFER,boneTexture_); glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,boneBuffer_);
        glUniform1i(glGetUniformLocation(prog,"uBones"),1); glActiveTexture(GL_TEXTURE0);
    }
}

// Configure a VAO with the interleaved Vertex attributes (from vbo) plus the
// per-instance transform columns (from the shared instance VBO).
void GLBackend::ConfigureMeshVao(unsigned int vao, unsigned int vbo) {
    const int stride = sizeof(Vertex);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,x)));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,u)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,stride,reinterpret_cast<void*>(offsetof(Vertex,r)));
    glEnableVertexAttribArray(7); glVertexAttribPointer(7,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,joints)));
    glEnableVertexAttribArray(8); glVertexAttribPointer(8,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,weights)));
    glEnableVertexAttribArray(9); glVertexAttribPointer(9,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(Vertex,nx)));
    glBindBuffer(GL_ARRAY_BUFFER,instanceVbo_);
    for(unsigned int column=0;column<4;++column) {
        glEnableVertexAttribArray(3+column);
        glVertexAttribPointer(3+column,4,GL_FLOAT,GL_FALSE,sizeof(Matrix),reinterpret_cast<void*>(column*4*sizeof(float)));
        glVertexAttribDivisor(3+column,1);
    }
}

unsigned int GLBackend::UploadMeshBuffer(const Vertex* verts, std::size_t count) {
    if(!verts || count==0) return 0;
    MeshBuffer mb; mb.count=count;
    glGenVertexArrays(1,&mb.vao);
    glGenBuffers(1,&mb.vbo);
    glBindVertexArray(mb.vao);
    glBindBuffer(GL_ARRAY_BUFFER,mb.vbo);
    glBufferData(GL_ARRAY_BUFFER,static_cast<std::ptrdiff_t>(count*sizeof(Vertex)),verts,GL_STATIC_DRAW);
    ConfigureMeshVao(mb.vao,mb.vbo);
    glBindVertexArray(vao_);
    const unsigned int handle=nextMeshBuffer_++;
    meshBuffers_[handle]=mb;
    return handle;
}

void GLBackend::DestroyMeshBuffer(unsigned int handle) {
    auto it=meshBuffers_.find(handle);
    if(it==meshBuffers_.end()) return;
    glDeleteBuffers(1,&it->second.vbo);
    glDeleteVertexArrays(1,&it->second.vao);
    meshBuffers_.erase(it);
}

bool GLBackend::DrawMeshBuffer(unsigned int handle, std::size_t count,
                               unsigned int textureId, const Matrix* transforms, int instances) {
    auto it=meshBuffers_.find(handle);
    if(it==meshBuffers_.end() || activeShader_) return false; // fall back
    const unsigned int prog=program_;
    // Load the per-instance transforms into the shared instance VBO first.
    std::vector<Matrix> xf(std::max(1,instances));
    for(int i=0;i<instances;++i) xf[i]=MatrixTranspose(transforms[i]);
    if(instances<=0) xf[0]=MatrixTranspose(MatrixIdentity());
    glBindBuffer(GL_ARRAY_BUFFER,instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<std::ptrdiff_t>(xf.size()*sizeof(Matrix)),xf.data(),GL_DYNAMIC_DRAW);
    ApplyBuiltinDrawState(prog,textureId);
    glBindVertexArray(it->second.vao);
    glDrawArraysInstanced(GL_TRIANGLES,0,static_cast<int>(count),std::max(1,instances));
    glBindVertexArray(vao_);
    return true;
}

// Directional shadow mapping: render a depth-only pass from the light's POV
// into a sampleable depth texture, then bind it (+ light matrix) for lit draws.
void GLBackend::BeginShadowPass(const Matrix& lightViewProj, int resolution) {
    resolution = resolution > 0 ? resolution : 1024;
    if(shadowFbo_ == 0 || resolution != shadowResolution_) {
        if(shadowDepthTex_) glDeleteTextures(1,&shadowDepthTex_);
        if(shadowFbo_) glDeleteFramebuffers(1,&shadowFbo_);
        glGenTextures(1,&shadowDepthTex_);
        glBindTexture(GL_TEXTURE_2D,shadowDepthTex_);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,resolution,resolution,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
        const float border[4]={1,1,1,1}; glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
        glGenFramebuffers(1,&shadowFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,shadowDepthTex_,0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        shadowResolution_ = resolution;
    }
    glGetIntegerv(GL_VIEWPORT,savedViewport_);
    glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo_);
    glViewport(0,0,resolution,resolution);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    // Render depth from the light: projection = lightViewProj, modelview = id.
    projection_ = lightViewProj; modelview_ = MatrixIdentity();
    shadowMatrix_ = lightViewProj;
    shadowPass_ = true;
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE); // depth only
}

void GLBackend::EndShadowPass() {
    if(!shadowPass_) return;
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(savedViewport_[0],savedViewport_[1],savedViewport_[2],savedViewport_[3]);
    shadowPass_ = false;
    shadowMap_ = shadowDepthTex_; // now available for lit sampling
}

// ---------------------------------------------------------------------------
// Cascaded shadow maps: N depth textures, one per view-frustum slice.
// ---------------------------------------------------------------------------
void GLBackend::BeginShadowCascades(int count, int resolution) {
    count = std::clamp(count, 1, kMaxCascades);
    resolution = resolution > 0 ? resolution : 2048;
    if(cascadeFbo_[0] == 0 || resolution != cascadeResolution_) {
        for(int i=0;i<kMaxCascades;++i) {
            if(cascadeTex_[i]) glDeleteTextures(1,&cascadeTex_[i]);
            if(cascadeFbo_[i]) glDeleteFramebuffers(1,&cascadeFbo_[i]);
            cascadeTex_[i]=cascadeFbo_[i]=0;
        }
        for(int i=0;i<count;++i) {
            glGenTextures(1,&cascadeTex_[i]);
            glBindTexture(GL_TEXTURE_2D,cascadeTex_[i]);
            glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,resolution,resolution,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
            const float border[4]={1,1,1,1}; glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
            glGenFramebuffers(1,&cascadeFbo_[i]);
            glBindFramebuffer(GL_FRAMEBUFFER,cascadeFbo_[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,cascadeTex_[i],0);
            glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        }
        cascadeResolution_ = resolution;
    }
    glGetIntegerv(GL_VIEWPORT,savedViewport_);
    cascadeCount_ = 0; // becomes valid after EndShadowCascades
    // Store the pending count in shadowResolution_-adjacent field via member.
    cascadeResolution_ = resolution;
    // Remember how many cascades this set uses (finalized in EndShadowCascades).
    pendingCascadeCount_ = count;
}

void GLBackend::BeginShadowCascade(int index, const Matrix& lightViewProj) {
    if(index < 0 || index >= kMaxCascades || cascadeFbo_[index]==0) return;
    cascadeMatrix_[index] = lightViewProj;
    glBindFramebuffer(GL_FRAMEBUFFER,cascadeFbo_[index]);
    glViewport(0,0,cascadeResolution_,cascadeResolution_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    projection_ = lightViewProj; modelview_ = MatrixIdentity();
    shadowPass_ = true;               // reuse depth-only draw state
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
}

void GLBackend::EndShadowCascade() {
    if(!shadowPass_) return;
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    shadowPass_ = false;
}

void GLBackend::EndShadowCascades(const float* splitDepths, int count, const Matrix& viewMatrix) {
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(savedViewport_[0],savedViewport_[1],savedViewport_[2],savedViewport_[3]);
    count = std::clamp(count, 1, kMaxCascades);
    for(int i=0;i<count;++i) cascadeSplit_[i]=splitDepths[i];
    cascadeViewMatrix_ = viewMatrix;
    cascadeCount_ = count;            // enable cascaded sampling on lit draws
    shadowMap_ = 0;                   // single-map path off while cascades active
}

void GLBackend::SetSkinning(const Matrix* bones,int count) {
    bones_.resize(std::max(0,count));
    for(int i=0;i<count;++i) bones_[i]=MatrixTranspose(bones[i]);
}
void GLBackend::DrawVerticesInstanced(const Vertex* verts, std::size_t count,
                                     unsigned int textureId, const Matrix* transforms, int instances) {
    if(activeShader_) { RenderBackend::DrawVerticesInstanced(verts,count,textureId,transforms,instances); return; }
    instances_.resize(instances);
    for(int i=0;i<instances;++i) instances_[i]=MatrixTranspose(transforms[i]);
    DrawVertices(verts,count,DrawMode::Triangles,textureId);
    instances_.clear();
}

static unsigned int CompressedGLFormat(PixelFormat format) {
    switch(format) {
        case PixelFormat::Compressed_DXT1_RGB: return 0x83F0;
        case PixelFormat::Compressed_DXT1_RGBA: return 0x83F1;
        case PixelFormat::Compressed_DXT3_RGBA: return 0x83F2;
        case PixelFormat::Compressed_DXT5_RGBA: return 0x83F3;
        case PixelFormat::Compressed_ETC1_RGB: return 0x8D64;
        case PixelFormat::Compressed_ETC2_RGB: return 0x9274;
        case PixelFormat::Compressed_ETC2_EAC_RGBA: return 0x9278;
        case PixelFormat::Compressed_ASTC_4x4_RGBA: return 0x93B0;
        case PixelFormat::Compressed_ASTC_8x8_RGBA: return 0x93B7;
        case PixelFormat::Compressed_PVRT_RGB:return 0x8C00;
        case PixelFormat::Compressed_PVRT_RGBA:return 0x8C02;
        default: return 0;
    }
}
bool GLBackend::SupportsTextureFormat(PixelFormat format) const {
    int value=static_cast<int>(format);
    if(value>=1 && value<=13) return true;
    unsigned int native=CompressedGLFormat(format);
    if(!native) return false;
    int count=0; glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS,&count);
    std::vector<int> formats(count); if(count) glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS,formats.data());
    return std::find(formats.begin(),formats.end(),static_cast<int>(native))!=formats.end();
}
unsigned int GLBackend::CreateTexture(const void* pixels, int width, int height,
                                      PixelFormat format) {
    const unsigned int compressed=CompressedGLFormat(format);
    if(compressed) {
        auto size=CompressedSize(width,height,format);
        if(!pixels || !size || !SupportsTextureFormat(format)) return 0;
        unsigned int id=0; glGenTextures(1,&id); glBindTexture(GL_TEXTURE_2D,id);
        glCompressedTexImage2D(GL_TEXTURE_2D,0,compressed,width,height,0,static_cast<int>(size),pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        return id;
    }
    auto upload=ConvertUpload(pixels,width,height,format);
    if(!upload.valid) return 0;
    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glTexImage2D(GL_TEXTURE_2D,0,upload.floating?GL_RGBA32F:GL_RGBA8,width,height,
                0,GL_RGBA,upload.floating?GL_FLOAT:GL_UNSIGNED_BYTE,upload.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

void GLBackend::DestroyTexture(unsigned int textureId) {
    cubemaps_.erase(textureId);
    if (textureId) glDeleteTextures(1, &textureId);
}

void GLBackend::UpdateTexture(unsigned int textureId, int width, int height,
                              PixelFormat format, const void* pixels) {
    const unsigned int compressed=CompressedGLFormat(format);
    if(compressed) {
        auto size=CompressedSize(width,height,format);
        if(!pixels || !size || !SupportsTextureFormat(format) || !glIsTexture(textureId)) return;
        glBindTexture(GL_TEXTURE_2D,textureId);
        glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,compressed,static_cast<int>(size),pixels);
        return;
    }
    auto upload=ConvertUpload(pixels,width,height,format);
    if(!pixels || !upload.valid || !glIsTexture(textureId)) return;
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,upload.floating?GL_FLOAT:GL_UNSIGNED_BYTE,upload.data());
}

int GLBackend::GenTextureMipmaps(unsigned int textureId) {
    if(!glIsTexture(textureId)) return 0;
    const unsigned int target=cubemaps_.contains(textureId)?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D;
    glBindTexture(target, textureId);
    glGenerateMipmap(target);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    int width=0,height=0;
    glGetTexLevelParameteriv(target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target,0,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target,0,GL_TEXTURE_HEIGHT,&height);
    int levels=1;
    for(int size=std::max(width,height);size>1;size/=2) ++levels;
    return levels;
}

void GLBackend::SetTextureFilter(unsigned int textureId, int filter) {
    const unsigned int target=cubemaps_.contains(textureId)?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D;
    glBindTexture(target, textureId);
    if(glfwExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        float maximum=1.0f; glGetFloatv(0x84FF,&maximum);
        const float requested=filter>=3?static_cast<float>(1u<<std::clamp(filter-1,2,4)):1.0f;
        glTexParameterf(target,0x84FE,std::min(maximum,requested));
    }
    if (filter == 0) { // point / nearest
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else if (filter == 2) { // trilinear
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else { // bilinear (default) + anisotropic tiers
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

void GLBackend::SetTextureWrap(unsigned int textureId, int wrap) {
#ifndef GL_MIRROR_CLAMP_TO_EDGE
#  define GL_MIRROR_CLAMP_TO_EDGE 0x8743
#endif
    unsigned int mode = GL_REPEAT;
    switch (wrap) {
        case 1: mode = GL_CLAMP_TO_EDGE; break;
        case 2: mode = GL_MIRRORED_REPEAT; break;
        case 3: mode = (glfwExtensionSupported("GL_EXT_texture_mirror_clamp") || glfwExtensionSupported("GL_ARB_texture_mirror_clamp_to_edge"))?GL_MIRROR_CLAMP_TO_EDGE:GL_CLAMP_TO_EDGE; break;
        default: mode = GL_REPEAT; break;
    }
    const unsigned int target=cubemaps_.contains(textureId)?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D;
    glBindTexture(target, textureId);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, static_cast<int>(mode));
    glTexParameteri(target, GL_TEXTURE_WRAP_T, static_cast<int>(mode));
}

// ---------------------------------------------------------------------------
// Framebuffers (render textures)
// ---------------------------------------------------------------------------
unsigned int GLBackend::CreateFramebuffer(int width, int height,
                                          unsigned int* colorTexOut) {
    if(colorTexOut) *colorTexOut=0;
    if(width<=0 || height<=0) return 0;
    int previousDraw=0,previousRead=0,previousTexture=0,previousRenderbuffer=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&previousRenderbuffer);
    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color attachment.
    unsigned int colorTex = 0;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           colorTex, 0);

    // Depth renderbuffer.
    unsigned int rbo = 0;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, rbo);

    const bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previousDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead);
    glBindTexture(GL_TEXTURE_2D,previousTexture);
    glBindRenderbuffer(GL_RENDERBUFFER,previousRenderbuffer);
    if(!complete) {
        glDeleteTextures(1,&colorTex); glDeleteRenderbuffers(1,&rbo); glDeleteFramebuffers(1,&fbo);
        return 0;
    }
    framebuffers_.emplace(fbo,Framebuffer{colorTex,rbo});
    if (colorTexOut) *colorTexOut = colorTex;
    return fbo;
}

void GLBackend::DestroyFramebuffer(unsigned int fboId, unsigned int colorTexId) {
    const auto it=framebuffers_.find(fboId);
    if(it==framebuffers_.end()) return;
    glDeleteTextures(1,&it->second.color);
    glDeleteRenderbuffers(1,&it->second.depth);
    glDeleteFramebuffers(1,&fboId);
    framebuffers_.erase(it);
}

void GLBackend::BindFramebuffer(unsigned int fboId, int width, int height) {
    if(fboId && !framebuffers_.contains(fboId)) return;
    offscreen_=fboId!=0;
    if(!offscreen_) glfwGetFramebufferSize(window_,&width,&height);
    glBindFramebuffer(GL_FRAMEBUFFER, fboId);
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
        fbWidth_ = width;
        fbHeight_ = height;
    } else {
        glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
        glViewport(0, 0, fbWidth_, fbHeight_);
    }
}

// ---------------------------------------------------------------------------
// Custom shaders
// ---------------------------------------------------------------------------
unsigned int GLBackend::CreateShaderProgram(const char* vsSrc, const char* fsSrc) {
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        std::fprintf(stderr, "[meowyrender][GL] shader program link failed\n");
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void GLBackend::DestroyShaderProgram(unsigned int programId) {
    if(activeShader_==programId) activeShader_=0;
    if (programId && programId != program_) glDeleteProgram(programId);
}

int GLBackend::GetShaderUniformLocation(unsigned int programId, const char* name) {
    return glGetUniformLocation(programId, name);
}

void GLBackend::SetShaderUniform(unsigned int programId, int location,
                                 const void* value, int uniformType, int count) {
    glUseProgram(programId);
    const auto* f = static_cast<const float*>(value);
    switch (uniformType) {
        case 0: glUniform1fv(location, count, f); break;
        case 1: glUniform2fv(location, count, f); break;
        case 2: glUniform3fv(location, count, f); break;
        case 3: glUniform4fv(location, count, f); break;
        case 4: glUniform1iv(location, count, static_cast<const int*>(value)); break;
        case 5: glUniformMatrix4fv(location,count,GL_TRUE,f);break;
        default: break;
    }
}

void GLBackend::SetActiveShader(unsigned int programId) {
    activeShader_ = programId;
}

Image GLBackend::ReadTexture(unsigned int textureId) {
    if(cubemaps_.contains(textureId))return ReadCubemapFace(textureId,0);
    if (!glIsTexture(textureId)) return {};
    GLint previous = 0, width = 0, height = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    Image image{};
    if (width > 0 && height > 0) {
        image.width = width; image.height = height;
        image.data = std::malloc(static_cast<std::size_t>(width) * height * 4);
        if (image.data) glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data);
    }
    glBindTexture(GL_TEXTURE_2D, previous);
    return image;
}
unsigned int GLBackend::CreateCubemap(const void* pixels,int size) {
    int maximum=0;glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE,&maximum);
    if(!pixels||size<=0||size>maximum)return 0;
    unsigned int texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
    for(int face=0;face<6;++face)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,static_cast<const unsigned char*>(pixels)+static_cast<size_t>(face)*size*size*4);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);cubemaps_[texture]=size;return texture;
}
Image GLBackend::ReadCubemapFace(unsigned int texture,int face) {
    auto found=cubemaps_.find(texture);if(found==cubemaps_.end()||face<0||face>=6)return {};
    Image image;image.width=image.height=found->second;image.data=std::malloc(static_cast<size_t>(image.width)*image.height*4);
    if(image.data){glBindTexture(GL_TEXTURE_CUBE_MAP,texture);glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA,GL_UNSIGNED_BYTE,image.data);}
    return image;
}
Image GLBackend::ReadScreen() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0) return {};
    Image image{}; image.width = width; image.height = height;
    image.data = std::malloc(static_cast<std::size_t>(width) * height * 4);
    if (!image.data) return {};
    GLint previous = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.data);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previous);
    auto* pixels = static_cast<unsigned char*>(image.data);
    for (int y = 0; y < height / 2; ++y)
        for (int x = 0; x < width * 4; ++x)
            std::swap(pixels[y * width * 4 + x], pixels[(height-1-y) * width * 4 + x]);
    return image;
}

#if defined(MEOWY_WITH_IMGUI)
// Dear ImGui on OpenGL: the GLFW platform backend + the GL3 renderer backend.
// ImGui's GL3 backend has its own loader, independent of our minimal loader.
bool GLBackend::ImGuiInit(void* glfwWindow) {
    auto* window = static_cast<GLFWwindow*>(glfwWindow ? glfwWindow : window_);
    if (!window) return false;
    glfwMakeContextCurrent(window);
    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) { ImGui::DestroyContext(); return false; }
    // GLSL 150 matches our 3.3 core context (forward-compatible on macOS).
    if (!ImGui_ImplOpenGL3_Init("#version 150")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    return true;
}
void GLBackend::ImGuiNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
void GLBackend::ImGuiRender() {
    ImGui::Render();
    // Draw into the default framebuffer (bound for the frame); the GL3 backend
    // saves and restores the GL state it touches.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
void GLBackend::ImGuiShutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
}
#endif // MEOWY_WITH_IMGUI

} // namespace meowyrender::backend::gl
