#version 450
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in mat4 aInstance;
layout(location = 7) in vec4 aJoints;
layout(location = 8) in vec4 aWeights;
layout(location = 9) in vec3 aNormal;
layout(set=0,binding=7,std430) readonly buffer Bones {mat4 matrices[];} bones;
layout(location = 0) out vec2 vTexcoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorld;
layout(location = 3) out vec3 vNormal;
layout(push_constant) uniform Transforms {
    mat4 projection;
    mat4 modelview;
} transforms;
vec3 normalTransform(mat4 matrix,vec3 normal) {mat3 m=mat3(matrix);return abs(determinant(m))>0.000001?transpose(inverse(m))*normal:normal;}
void main() {
    vec4 position=vec4(aPosition,1);vec3 normal=aNormal;float sum=dot(aWeights,vec4(1));
    if(sum>0) {
        position=vec4(0);normal=vec3(0);
        for(int i=0;i<4;++i)if(aWeights[i]>0) {
            mat4 skin=bones.matrices[int(aJoints[i])];float weight=aWeights[i]/sum;
            position+=skin*vec4(aPosition,1)*weight;normal+=normalTransform(skin,aNormal)*weight;
        }
    }
    vec4 world=aInstance*position;vWorld=world.xyz;vNormal=normalTransform(aInstance,normal);
    gl_Position = transforms.projection * transforms.modelview * world;
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    vTexcoord = aTexcoord;
    vColor = aColor;
}
