#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../pbr.glsl"
layout(location = 0) in vec2 vTexcoord;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vWorld;
layout(location = 3) in vec3 vNormal;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D albedo;
layout(set=0,binding=1,std140) uniform Surface {
    vec4 eye,direction,radiance,ambient,emission;float metallic,roughness;uint enabled,mask;float alphaCutoff;
    float envIntensity;uint envMips;uint hasEnv;
    // hasShadow at a plain uint slot; pad so shadowMatrix is 16-byte aligned.
    uint hasShadow;uint padB,padC,padD;
    mat4 shadowMatrix;
} light;
layout(set=0,binding=2) uniform sampler2D metalMap;
layout(set=0,binding=3) uniform sampler2D roughMap;
layout(set=0,binding=4) uniform sampler2D normalMap;
layout(set=0,binding=5) uniform sampler2D aoMap;
layout(set=0,binding=6) uniform sampler2D emissionMap;
layout(set=0,binding=8) uniform samplerCube environmentMap;
layout(set=0,binding=9) uniform sampler2D shadowMap;

// 3x3 PCF directional shadow factor (1 = lit, 0 = fully shadowed).
float shadowFactor(vec3 world,vec3 n,vec3 l) {
    // Match shadow.vert exactly (no Y flip): project world into light clip
    // space, remap Z to [0,w]/w, then to [0,1] UV. The rasterizer's viewport
    // transform for the shadow pass and this lookup use the same convention.
    vec4 lightClip = light.shadowMatrix * vec4(world,1.0);
    lightClip.z = (lightClip.z + lightClip.w) * 0.5;
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0||proj.z>1.0) return 1.0;
    float bias = max(0.0025*(1.0-dot(n,l)),0.0005);
    float current = proj.z - bias;
    float sum = 0.0;
    float texel = 1.0/float(textureSize(shadowMap,0).x);
    for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) {
        float closest = texture(shadowMap, uv + vec2(x,y)*texel).r;
        sum += current <= closest ? 1.0 : 0.0;
    }
    return sum/9.0; // 1 = lit, 0 = shadowed
}
void main() {
    vec4 color=texture(albedo,vTexcoord)*vColor;
    // glTF alpha MASK: discard fragments below the cutoff.
    if(light.alphaCutoff>0.0 && color.a<light.alphaCutoff) discard;
    if(light.enabled==0){outColor=color;return;}
    vec3 normal=dot(vNormal,vNormal)>0.000001?safeNormal(vNormal):safeNormal(cross(dFdx(vWorld),dFdy(vWorld)));
    if((light.mask&4)!=0) {
        vec3 sampled=texture(normalMap,vTexcoord).xyz*2-1;
        vec3 px=dFdx(vWorld),py=dFdy(vWorld);vec2 tx=dFdx(vTexcoord),ty=dFdy(vTexcoord);
        float determinant=tx.x*ty.y-tx.y*ty.x;
        if(abs(determinant)>0.000001)normal=safeNormal(safeNormal((px*ty.y-py*tx.y)/determinant)*sampled.x+safeNormal((py*tx.x-px*ty.x)/determinant)*sampled.y+normal*sampled.z);
    }
    float metallic=light.metallic*((light.mask&1)!=0?texture(metalMap,vTexcoord).b:1);
    float roughness=light.roughness*((light.mask&2)!=0?texture(roughMap,vTexcoord).g:1);
    float ao=(light.mask&8)!=0?texture(aoMap,vTexcoord).r:1;
    vec3 emission=light.emission.xyz*((light.mask&16)!=0?pow(texture(emissionMap,vTexcoord).rgb,vec3(2.2)):vec3(1));
    vec3 baseLinear=pow(max(color.rgb,vec3(0)),vec3(2.2));
    // Directional shadow factor (1 = lit) modulates the direct radiance.
    float shadow=1.0;
    if(light.hasShadow!=0u) shadow=shadowFactor(vWorld,normal,safeNormal(-light.direction.xyz));
    vec3 directRadiance=light.radiance.xyz*shadow;
    if(light.hasEnv!=0) {
        // Image-based lighting: sample the environment cubemap for the diffuse
        // (normal) and specular (reflection) ambient terms, mirroring the
        // OpenGL/Metal lit path. Roughness selects a blurred reflection mip.
        vec3 view=safeNormal(light.eye.xyz-vWorld);
        vec3 refl=reflect(-view,normal);
        vec3 envDiffuse=pow(texture(environmentMap,normal).rgb,vec3(2.2))*light.envIntensity;
        float lod=roughness*float(max(int(light.envMips)-1,0));
        vec3 envSpecular=pow((light.envMips>1u?textureLod(environmentMap,refl,lod):texture(environmentMap,refl)).rgb,vec3(2.2))*light.envIntensity;
        outColor=vec4(shadePBRIBL(baseLinear,normal,vWorld,light.eye.xyz,light.direction.xyz,directRadiance,emission,metallic,roughness,ao,envDiffuse,envSpecular),color.a);
        return;
    }
    outColor=vec4(shadePBR(baseLinear,normal,vWorld,light.eye.xyz,light.direction.xyz,directRadiance,light.ambient.xyz,emission,metallic,roughness,ao),color.a);
}
