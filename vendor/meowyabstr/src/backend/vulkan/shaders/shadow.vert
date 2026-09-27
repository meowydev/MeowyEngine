#version 450
// Depth-only vertex shader for the directional shadow pass. Applies the same
// skinning + per-instance transform as batch.vert, but projects with the
// light's view-projection (pushed in the projection slot).
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in mat4 aInstance;
layout(location = 7) in vec4 aJoints;
layout(location = 8) in vec4 aWeights;
layout(location = 9) in vec3 aNormal;
layout(set=0,binding=7,std430) readonly buffer Bones {mat4 matrices[];} bones;
layout(push_constant) uniform Transforms {
    mat4 projection;   // light view-projection
    mat4 modelview;    // identity during the shadow pass
} transforms;
layout(location = 0) out float vDepth;  // linear-in-clip depth written to R32F
void main() {
    vec4 position=vec4(aPosition,1);float sum=dot(aWeights,vec4(1));
    if(sum>0) {
        position=vec4(0);
        for(int i=0;i<4;++i)if(aWeights[i]>0)
            position+=bones.matrices[int(aJoints[i])]*vec4(aPosition,1)*(aWeights[i]/sum);
    }
    vec4 world=aInstance*position;
    gl_Position = transforms.projection * transforms.modelview * world;
    // No Y flip here: shadowFactor() in batch.frag samples with the same
    // (un-flipped) light-space math, so write and read stay consistent.
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vDepth = gl_Position.z / gl_Position.w;
}
