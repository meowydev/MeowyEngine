#include "meowyrender/meowyrender.hpp"
#include "modules/gltf_support.hpp"
#include <memory>
#include <algorithm>
#include <cstring>
#include <climits>

namespace meowyrender {
namespace {
Quaternion Slerp(Quaternion a,Quaternion b,float t) {
    a=detail::NormalizeRotation(a); b=detail::NormalizeRotation(b);
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0) {b={-b.x,-b.y,-b.z,-b.w}; dot=-dot;}
    float left=1-t,right=t;
    if(dot<0.9995f) {float angle=std::acos(std::clamp(dot,-1.0f,1.0f)); float inverse=1/std::sin(angle); left=std::sin((1-t)*angle)*inverse; right=std::sin(t*angle)*inverse;}
    return detail::NormalizeRotation({left*a.x+right*b.x,left*a.y+right*b.y,left*a.z+right*b.z,left*a.w+right*b.w});
}
void Sample(const cgltf_animation_channel& channel,float time,Transform& pose) {
    auto& sampler=*channel.sampler;
    if(!sampler.input || !sampler.output || !sampler.input->count) return;
    // Morph-weight animation channels are skipped here (skeletal poses only);
    // morph weights are driven programmatically via SetMeshMorphWeights. Models
    // that also animate weights still load and animate their skeletons.
    if(channel.target_path==cgltf_animation_path_type_weights) return;
    const int components=channel.target_path==cgltf_animation_path_type_rotation?4:3;
    std::size_t lo=0,hi=sampler.input->count;
    while(lo<hi) {auto mid=lo+(hi-lo)/2; float t=0; cgltf_accessor_read_float(sampler.input,mid,&t,1); if(t<=time) lo=mid+1; else hi=mid;}
    std::size_t first=lo?lo-1:0,last=std::min(first+1,sampler.input->count-1);
    float start=0,end=0; cgltf_accessor_read_float(sampler.input,first,&start,1); cgltf_accessor_read_float(sampler.input,last,&end,1);
    float duration=end-start,t=duration>0?std::clamp((time-start)/duration,0.0f,1.0f):0;
    const bool cubic=sampler.interpolation==cgltf_interpolation_type_cubic_spline;
    float a[4]{},b[4]{},value[4]{};
    cgltf_accessor_read_float(sampler.output,cubic?first*3+1:first,a,components);
    cgltf_accessor_read_float(sampler.output,cubic?last*3+1:last,b,components);
    if(sampler.interpolation==cgltf_interpolation_type_step || first==last) std::copy_n(a,components,value);
    else if(cubic) {
        float outgoing[4]{},incoming[4]{};
        cgltf_accessor_read_float(sampler.output,first*3+2,outgoing,components);
        cgltf_accessor_read_float(sampler.output,last*3,incoming,components);
        float t2=t*t,t3=t2*t;
        for(int c=0;c<components;++c) value[c]=(2*t3-3*t2+1)*a[c]+(t3-2*t2+t)*duration*outgoing[c]+(-2*t3+3*t2)*b[c]+(t3-t2)*duration*incoming[c];
    } else if(components==4) {
        auto q=Slerp({a[0],a[1],a[2],a[3]},{b[0],b[1],b[2],b[3]},t); value[0]=q.x; value[1]=q.y; value[2]=q.z; value[3]=q.w;
    } else for(int c=0;c<components;++c) value[c]=a[c]+t*(b[c]-a[c]);
    if(channel.target_path==cgltf_animation_path_type_translation) pose.translation={value[0],value[1],value[2]};
    if(channel.target_path==cgltf_animation_path_type_scale) pose.scale={value[0],value[1],value[2]};
    if(channel.target_path==cgltf_animation_path_type_rotation) pose.rotation=detail::NormalizeRotation({value[0],value[1],value[2],value[3]});
}
// Sample a morph-weight channel at `time` into `weights` (length targetCount).
void SampleWeights(const cgltf_animation_channel& channel,float time,int targetCount,float* weights) {
    auto& sampler=*channel.sampler;
    if(!sampler.input || !sampler.output || !sampler.input->count) return;
    std::size_t lo=0,hi=sampler.input->count;
    while(lo<hi){auto mid=lo+(hi-lo)/2; float t=0; cgltf_accessor_read_float(sampler.input,mid,&t,1); if(t<=time) lo=mid+1; else hi=mid;}
    std::size_t first=lo?lo-1:0,last=std::min(first+1,sampler.input->count-1);
    float start=0,end=0; cgltf_accessor_read_float(sampler.input,first,&start,1); cgltf_accessor_read_float(sampler.input,last,&end,1);
    float dur=end-start,t=dur>0?std::clamp((time-start)/dur,0.0f,1.0f):0;
    const bool cubic=sampler.interpolation==cgltf_interpolation_type_cubic_spline;
    const bool step=sampler.interpolation==cgltf_interpolation_type_step;
    for(int k=0;k<targetCount;++k) {
        float a=0,b=0;
        cgltf_accessor_read_float(sampler.output,(cubic?first*3+1:first)*targetCount+k,&a,1);
        cgltf_accessor_read_float(sampler.output,(cubic?last*3+1:last)*targetCount+k,&b,1);
        weights[k]=(step||first==last)?a:a+t*(b-a);
    }
}
} // namespace
ModelAnimation* LoadModelAnimations(const std::string& fileName,int* animCount) {
    if(animCount) *animCount=0;
    cgltf_options options{}; cgltf_data* raw=nullptr;
    if(cgltf_parse_file(&options,fileName.c_str(),&raw)!=cgltf_result_success) return nullptr;
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    if(cgltf_load_buffers(&options,raw,fileName.c_str())!=cgltf_result_success || cgltf_validate(raw)!=cgltf_result_success || !raw->animations_count) return nullptr;
    if(raw->animations_count>INT_MAX||raw->nodes_count>INT_MAX)throw std::length_error("glTF exceeds animation limits");
    const int count=static_cast<int>(raw->animations_count);
    size_t totalSamples=0;
    auto* result=new ModelAnimation[count]{};
    try {
        for(int index=0;index<count;++index) {
            auto& source=raw->animations[index]; auto& animation=result[index];
            animation.boneCount=static_cast<int>(raw->nodes_count);
            animation.bones=new BoneInfo[animation.boneCount];
            if(source.name) std::snprintf(animation.name,sizeof(animation.name),"%s",source.name);
            for(cgltf_size c=0;c<source.channels_count;++c) {
                const auto* input=source.channels[c].sampler->input; float end=0;
                if(input)for(cgltf_size key=0;key<input->count;++key){
                    float time=0;
                    if(!cgltf_accessor_read_float(input,key,&time,1)||!std::isfinite(time)||time<0||(key&&time<=end))throw std::runtime_error("Invalid animation key times");
                    end=time;
                }
                if(!std::isfinite(end) || end<0) throw std::runtime_error("Invalid animation duration");
                animation.duration=std::max(animation.duration,end);
            }
            if(animation.duration>3600) throw std::runtime_error("Animation exceeds one-hour sampling limit");
            animation.frameCount=static_cast<int>(std::ceil(animation.duration*animation.frameRate))+1;
            totalSamples+=static_cast<size_t>(animation.frameCount)*animation.boneCount;
            if(totalSamples>2000000)
                throw std::runtime_error("Animation exceeds sampled pose memory limit");
            animation.framePoses=new Transform*[animation.frameCount]{};
            animation.frameMatrices=new Matrix*[animation.frameCount]{};
            // Detect a morph-weight channel and its target count (mesh weights).
            const cgltf_animation_channel* weightChannel=nullptr;
            for(cgltf_size c=0;c<source.channels_count;++c)
                if(source.channels[c].target_path==cgltf_animation_path_type_weights && source.channels[c].target_node && source.channels[c].target_node->mesh) {
                    weightChannel=&source.channels[c];
                    animation.morphTargetCount=static_cast<int>(source.channels[c].target_node->mesh->weights_count);
                    if(animation.morphTargetCount==0 && source.channels[c].target_node->mesh->primitives_count)
                        animation.morphTargetCount=static_cast<int>(source.channels[c].target_node->mesh->primitives[0].targets_count);
                    break;
                }
            if(weightChannel && animation.morphTargetCount>0)
                animation.frameWeights=new float*[animation.frameCount]{};
            for(int n=0;n<animation.boneCount;++n) animation.bones[n]=detail::NodeBone(*raw,raw->nodes[n]);
            for(int frame=0;frame<animation.frameCount;++frame) {
                auto* poses=animation.framePoses[frame]=new Transform[animation.boneCount];
                auto* matrices=animation.frameMatrices[frame]=new Matrix[animation.boneCount];
                for(int n=0;n<animation.boneCount;++n) poses[n]=detail::NodePose(raw->nodes[n]);
                float time=std::min(frame/animation.frameRate,animation.duration);
                for(cgltf_size c=0;c<source.channels_count;++c) {
                    const auto& channel=source.channels[c];
                    if(channel.target_node) {
                        if(channel.target_node->has_matrix) throw std::runtime_error("Animated glTF nodes must use TRS, not a matrix");
                        Sample(channel,time,poses[channel.target_node-raw->nodes]);
                    }
                }
                for(int n=0;n<animation.boneCount;++n) matrices[n]=raw->nodes[n].has_matrix?detail::GltfMatrix(raw->nodes[n].matrix):detail::TransformMatrix(poses[n]);
                if(animation.frameWeights) {
                    auto* w=animation.frameWeights[frame]=new float[animation.morphTargetCount]{};
                    SampleWeights(*weightChannel,time,animation.morphTargetCount,w);
                }
            }
        }
    } catch(...) {UnloadModelAnimations(result,count); throw;}
    if(animCount) *animCount=count;
    return result;
}
bool IsModelAnimationValid(Model model,ModelAnimation animation) {
    if(model.boneCount<=0 || model.boneCount!=animation.boneCount || !model.bones || !animation.bones || !animation.framePoses || animation.frameCount<=0) return false;
    for(int i=0;i<model.boneCount;++i) if(model.bones[i].parent!=animation.bones[i].parent || std::strncmp(model.bones[i].name,animation.bones[i].name,sizeof(BoneInfo::name))) return false;
    return true;
}
void UpdateModelAnimation(Model model,ModelAnimation animation,int frame) {
    if(!IsModelAnimationValid(model,animation)) return;
    frame=((frame%animation.frameCount)+animation.frameCount)%animation.frameCount;
    std::vector<Matrix> local(model.boneCount);
    for(int i=0;i<model.boneCount;++i) local[i]=animation.frameMatrices?animation.frameMatrices[frame][i]:detail::TransformMatrix(animation.framePoses[frame][i]);
    detail::ApplyPalette(model,local.data());
    // Drive morph-target weights from the animation's weight tracks, if present.
    if(animation.frameWeights && animation.morphTargetCount>0) {
        const float* w=animation.frameWeights[frame];
        for(int m=0;m<model.meshCount;++m)
            if(model.meshes[m].morphTargetCount>0)
                SetMeshMorphWeights(&model.meshes[m],w,std::min(animation.morphTargetCount,model.meshes[m].morphTargetCount));
    }
}
void UpdateModelAnimationEx(Model model,ModelAnimation animation,int frame,bool updateBones) {
    // updateBones only affects whether the bone node poses are also written;
    // MeowyRender's palette update already covers the GPU skin path, so the
    // flag is honored by always applying the palette (bones are updated).
    (void)updateBones;
    UpdateModelAnimation(model,animation,frame);
}
void UnloadModelAnimations(ModelAnimation* animations,int count) {
    if(!animations) return;
    for(int i=0;i<count;++i) {
        auto& a=animations[i];
        for(int frame=0;frame<a.frameCount;++frame) {if(a.framePoses) delete[] a.framePoses[frame]; if(a.frameMatrices) delete[] a.frameMatrices[frame]; if(a.frameWeights) delete[] a.frameWeights[frame];}
        delete[] a.framePoses; delete[] a.frameMatrices; delete[] a.frameWeights; delete[] a.bones;
    }
    delete[] animations;
}
}
