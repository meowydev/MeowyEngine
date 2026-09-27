#pragma once
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <mutex>
#include <vector>
#include <string>
#include <stdexcept>

namespace meowyrender::backend::vulkan {
struct RuntimeShader {
    struct Uniform {std::string name; int offset, size, stride, type;};
    std::vector<unsigned int> vertex,fragment;
    std::vector<Uniform> uniforms;
    std::vector<unsigned char> values;
};
inline RuntimeShader CompileShader(const char* vs,const char* fs) {
    static std::once_flag initialized;
    std::call_once(initialized,[]{if(!glslang::InitializeProcess()) throw std::runtime_error("glslang initialization failed");});
    glslang::TShader vertex(EShLangVertex),fragment(EShLangFragment);
    glslang::TProgram program;
    const auto messages=static_cast<EShMessages>(EShMsgSpvRules|EShMsgVulkanRules);
    for(auto pair:{std::pair{&vertex,vs},std::pair{&fragment,fs}}) {
        if(!pair.second||!*pair.second) throw std::runtime_error("Vulkan shaders require vertex and fragment GLSL 450 sources");
        auto* shader=pair.first; shader->setStrings(&pair.second,1);
        shader->setEnvInput(glslang::EShSourceGlsl,shader==&vertex?EShLangVertex:EShLangFragment,glslang::EShClientVulkan,450);
        shader->setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_0);
        shader->setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_0);
        if(!shader->parse(GetDefaultResources(),450,false,messages)) throw std::runtime_error(shader->getInfoLog());
        program.addShader(shader);
    }
    if(!program.link(messages)||!program.buildReflection(EShReflectionAllBlockVariables)) throw std::runtime_error(program.getInfoLog());
    RuntimeShader result;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex),result.vertex);
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment),result.fragment);
    // Reject resource layouts outside the renderer's explicit shader ABI before
    // they reach Vulkan pipeline creation. Set 0: albedo at 0, std140 data at 1.
    for(auto* code:{&result.vertex,&result.fragment}) for(size_t i=5;i<code->size();) {
        unsigned n=(*code)[i]>>16,op=(*code)[i]&65535;
        if(!n||i+n>code->size()) throw std::runtime_error("Malformed compiler SPIR-V");
        if(op==71&&n>=4) {
            if((*code)[i+2]==34&&(*code)[i+3]!=0) throw std::runtime_error("Custom shaders require descriptor set 0");
            if((*code)[i+2]==33&&(*code)[i+3]>1) throw std::runtime_error("Custom shaders support bindings 0 and 1");
        }
        i+=n;
    }
    if(program.getNumBufferBlocks()) throw std::runtime_error("Custom storage buffers are not exposed by this API");
    for(int i=0;i<program.getNumLiveUniformBlocks();++i) {
        int binding=program.getUniformBlockBinding(i),size=program.getUniformBlockSize(i);
        if(binding<0) {if(size>128) throw std::runtime_error("Transform push constants exceed 128 bytes"); continue;}
        if(binding!=1||size>16384) throw std::runtime_error("Custom uniform data must use binding 1 and fit in 16 KiB");
        result.values.resize(std::max(size,16));
    }
    for(int i=0;i<program.getNumLiveUniformVariables();++i) {
        const auto& uniform=program.getUniform(i);
        int block=program.getUniformBlockIndex(i);
        if(block<0) {if(uniform.getBinding()!=0||(uniform.glDefineType!=0x8B5E&&uniform.glDefineType!=0x8B60)) throw std::runtime_error("Binding 0 must be sampler2D or samplerCube"); continue;}
        if(program.getUniformBlockBinding(block)!=1) continue;
        int type=-1;
        switch(uniform.glDefineType) {case 0x1406:type=0;break;case 0x8B50:type=1;break;case 0x8B51:type=2;break;case 0x8B52:type=3;break;case 0x1404:type=4;break;case 0x8B5C:type=5;break;}
        if(type<0) throw std::runtime_error("Custom uniforms support float, vec2/3/4, int and arrays of these types");
        result.uniforms.push_back({uniform.name,uniform.offset,std::max(uniform.size,1),uniform.arrayStride,type});
    }
    if(result.values.empty()) result.values.resize(16);
    return result;
}
}
