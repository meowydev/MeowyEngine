#pragma once
#include "meowyrender/mr_types.hpp"
#include "cgltf.h"
#include <cmath>
#include <vector>
#include <functional>
#include <stdexcept>
#include <cstdio>

namespace meowyrender::detail {
inline Matrix GltfMatrix(const float* a) {
    return {a[0],a[4],a[8],a[12],a[1],a[5],a[9],a[13],a[2],a[6],a[10],a[14],a[3],a[7],a[11],a[15]};
}
inline Quaternion NormalizeRotation(Quaternion q) {
    float length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return length>0?Quaternion{q.x/length,q.y/length,q.z/length,q.w/length}:Quaternion{0,0,0,1};
}
inline Matrix TransformMatrix(Transform t) {
    auto q=NormalizeRotation(t.rotation);
    Matrix r;
    r.m0=1-2*(q.y*q.y+q.z*q.z); r.m4=2*(q.x*q.y-q.z*q.w); r.m8=2*(q.x*q.z+q.y*q.w);
    r.m1=2*(q.x*q.y+q.z*q.w); r.m5=1-2*(q.x*q.x+q.z*q.z); r.m9=2*(q.y*q.z-q.x*q.w);
    r.m2=2*(q.x*q.z-q.y*q.w); r.m6=2*(q.y*q.z+q.x*q.w); r.m10=1-2*(q.x*q.x+q.y*q.y);
    return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x,t.scale.y,t.scale.z),r),MatrixTranslate(t.translation.x,t.translation.y,t.translation.z));
}
inline Transform NodePose(const cgltf_node& node) {
    Transform pose;
    if(node.has_translation) pose.translation={node.translation[0],node.translation[1],node.translation[2]};
    if(node.has_rotation) pose.rotation={node.rotation[0],node.rotation[1],node.rotation[2],node.rotation[3]};
    if(node.has_scale) pose.scale={node.scale[0],node.scale[1],node.scale[2]};
    return pose;
}
inline BoneInfo NodeBone(const cgltf_data& data,const cgltf_node& node) {
    BoneInfo bone;
    if(node.name) std::snprintf(bone.name,sizeof(bone.name),"%s",node.name);
    bone.parent=node.parent?static_cast<int>(node.parent-data.nodes):-1;
    return bone;
}
inline std::vector<Matrix> WorldMatrices(const BoneInfo* bones,const Matrix* local,int count) {
    std::vector<Matrix> world(count);
    std::vector<unsigned char> visited(count);
    std::vector<int> path;
    for(int i=0;i<count;++i){
        int node=i;path.clear();
        while(node>=0&&visited[node]!=2){
            if(visited[node]==1)throw std::runtime_error("Cyclic bone hierarchy");
            visited[node]=1;path.push_back(node);
            node=bones[node].parent;
            if(node>=count||node<-1)throw std::runtime_error("Invalid bone parent");
        }
        for(auto it=path.rbegin();it!=path.rend();++it){
            int index=*it,parent=bones[index].parent;
            world[index]=parent>=0?MatrixMultiply(local[index],world[parent]):local[index];visited[index]=2;
        }
    }
    return world;
}
inline void ApplyPalette(Model model,const Matrix* local) {
    auto world=WorldMatrices(model.bones,local,model.boneCount);
    for(int i=0;i<model.meshCount;++i) {
        auto& mesh=model.meshes[i];
        for(int j=0;j<mesh.boneCount;++j) {
            const int node=mesh.boneNodes[j];
            if(node>=0 && node<model.boneCount) mesh.boneMatrices[j]=MatrixMultiply(mesh.inverseBindMatrices[j],world[node]);
        }
    }
}
}
