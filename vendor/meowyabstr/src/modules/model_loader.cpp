// meowyrender - src/modules/model_loader.cpp
// Model file loading: a hand-written Wavefront OBJ parser plus glTF 2.0 loading
// through cgltf. Loaded geometry is expanded into meowyrender Mesh arrays
// (3 verts per triangle for the immediate rendering path).
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include "cgltf.h"
#include "modules/gltf_support.hpp"
#include <memory>
#include <filesystem>
#include <climits>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>

namespace meowyrender {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool EndsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Build a meowyrender Mesh from flat vertex/uv/normal arrays (already expanded
// to 3 verts per triangle).
Mesh MakeMesh(const std::vector<float>& verts,
              const std::vector<float>& uvs,
              const std::vector<float>& normals) {
    Mesh m;
    if(verts.size()/3>INT_MAX)throw std::length_error("Mesh exceeds vertex limit");
    m.vertexCount = static_cast<int>(verts.size() / 3);
    m.triangleCount = m.vertexCount / 3;
    m.vertices = static_cast<float*>(std::malloc(verts.size() * sizeof(float)));
    if(!m.vertices && !verts.empty())throw std::bad_alloc();
    std::memcpy(m.vertices, verts.data(), verts.size() * sizeof(float));
    if (!uvs.empty()) {
        m.texcoords = static_cast<float*>(std::malloc(uvs.size() * sizeof(float)));
        if(!m.texcoords){std::free(m.vertices);throw std::bad_alloc();}
        std::memcpy(m.texcoords, uvs.data(), uvs.size() * sizeof(float));
    }
    if (!normals.empty()) {
        m.normals = static_cast<float*>(std::malloc(normals.size() * sizeof(float)));
        if(!m.normals){std::free(m.vertices);std::free(m.texcoords);throw std::bad_alloc();}
        std::memcpy(m.normals, normals.data(), normals.size() * sizeof(float));
    }
    return m;
}

Model SingleMeshModel(Mesh mesh) {
    Model model;
    model.transform = MatrixIdentity();
    model.meshCount = 1;
    model.materialCount = 1;
    model.meshes = new Mesh[1]{mesh};
    model.materials = new Material[1]{LoadMaterialDefault()};
    model.meshMaterial = new int[1]{0};
    return model;
}

// -----------------------------------------------------------------------------
// Wavefront OBJ parser (positions, texcoords, normals; triangulates faces).
// -----------------------------------------------------------------------------
Model LoadOBJ(const std::string& fileName) {
    std::ifstream file(fileName);
    if (!file) {
        std::fprintf(stderr, "[meowyrender] LoadOBJ: cannot open %s\n", fileName.c_str());
        return {};
    }

    std::vector<float> positions;  // xyz triples
    std::vector<float> texcoords;  // uv pairs
    std::vector<float> normals;    // xyz triples

    std::vector<float> outV, outT, outN;

    // A face vertex references v/vt/vn indices (1-based, possibly negative).
    auto resolve = [](int idx, int count) {
        auto resolved=idx>0?static_cast<int64_t>(idx)-1:static_cast<int64_t>(count)+idx;
        if(idx==0 || resolved<0 || resolved>=count)throw std::invalid_argument("OBJ face index is outside its attribute array");
        return static_cast<int>(resolved);
    };

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            float x=0,y=0,z=0;
            if(!(ss>>x>>y>>z)||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::invalid_argument("Invalid OBJ vertex");
            positions.insert(positions.end(), {x, y, z});
        } else if (tag == "vt") {
            float u=0,v=0;
            if(!(ss>>u))throw std::invalid_argument("Invalid OBJ texture coordinate");
            ss>>v;
            if(!std::isfinite(u)||!std::isfinite(v))throw std::invalid_argument("Invalid OBJ texture coordinate");
            texcoords.insert(texcoords.end(), {u, v});
        } else if (tag == "vn") {
            float x=0,y=0,z=0;
            if(!(ss>>x>>y>>z)||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::invalid_argument("Invalid OBJ normal");
            normals.insert(normals.end(), {x, y, z});
        } else if (tag == "f") {
            // Read all face vertices, then fan-triangulate.
            struct FV { int v = 0, t = 0, n = 0; };
            std::vector<FV> face;
            std::string tok;
            while (ss >> tok) {
                if(tok.starts_with('#'))break;
                FV fv;
                // formats: v, v/t, v//n, v/t/n
                int v = 0, t = 0, n = 0;
                if (std::sscanf(tok.c_str(), "%d/%d/%d", &v, &t, &n) == 3) {}
                else if (std::sscanf(tok.c_str(), "%d//%d", &v, &n) == 2) {}
                else if (std::sscanf(tok.c_str(), "%d/%d", &v, &t) == 2) {}
                else std::sscanf(tok.c_str(), "%d", &v);
                fv.v = v; fv.t = t; fv.n = n;
                face.push_back(fv);
            }
            const int posCount = static_cast<int>(positions.size() / 3);
            const int uvCount = static_cast<int>(texcoords.size() / 2);
            const int nrmCount = static_cast<int>(normals.size() / 3);
            auto emit = [&](const FV& fv) {
                const int vi = resolve(fv.v, posCount);
                outV.insert(outV.end(), {positions[vi*3+0], positions[vi*3+1], positions[vi*3+2]});
                if (fv.t != 0) {
                    const int ti = resolve(fv.t, uvCount);
                    outT.insert(outT.end(), {texcoords[ti*2+0], texcoords[ti*2+1]});
                } else {
                    outT.insert(outT.end(), {0.0f, 0.0f});
                }
                if (fv.n != 0) {
                    const int ni = resolve(fv.n, nrmCount);
                    outN.insert(outN.end(), {normals[ni*3+0], normals[ni*3+1], normals[ni*3+2]});
                } else {
                    outN.insert(outN.end(), {0.0f, 1.0f, 0.0f});
                }
            };
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                emit(face[0]); emit(face[i]); emit(face[i + 1]);
            }
        }
    }

    if (outV.empty()) {
        std::fprintf(stderr, "[meowyrender] LoadOBJ: no geometry in %s\n", fileName.c_str());
        return {};
    }
    Mesh mesh = MakeMesh(outV, outT, outN);
    std::printf("[meowyrender] loaded OBJ %s: %d verts, %d tris\n",
                fileName.c_str(), mesh.vertexCount, mesh.triangleCount);
    return SingleMeshModel(mesh);
}

// -----------------------------------------------------------------------------
// glTF 2.0 loading via cgltf.
// -----------------------------------------------------------------------------
void ReadAccessorFloats(const cgltf_accessor* acc, std::vector<float>& out, int comps) {
    if (!acc) return;
    const std::size_t count = acc->count;
    out.resize(count * comps);
    for (std::size_t i = 0; i < count; ++i)
        cgltf_accessor_read_float(acc, i, &out[i * comps], comps);
}

Texture2D LoadGltfTexture(const cgltf_texture* texture,const std::string& fileName) {
    if(!texture || !texture->image || !detail::State().backend) return {};
    const auto* image=texture->image; Image decoded{};
    if(image->buffer_view) {
        if(image->buffer_view->size>INT_MAX) return {};
        auto* bytes=cgltf_buffer_view_data(image->buffer_view);
        if(bytes) decoded=LoadImageFromMemory("",bytes,static_cast<int>(image->buffer_view->size));
    } else if(image->uri) {
        std::string uri=image->uri;
        if(uri.starts_with("data:")) {
            auto comma=uri.find(',');
            if(comma==std::string::npos || uri.substr(0,comma).find(";base64")==std::string::npos) return {};
            std::vector<unsigned char> bytes;
            unsigned int accumulator=0; int bits=0;
            for(std::size_t i=comma+1;i<uri.size() && uri[i]!='=';++i) {
                auto c=uri[i]; int digit=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:c=='/'?63:-1;
                if(digit<0) continue;
                accumulator=(accumulator<<6)|static_cast<unsigned int>(digit); bits+=6;
                if(bits>=8) {bits-=8;bytes.push_back(static_cast<unsigned char>(accumulator>>bits));}
            }
            if(bytes.size()<=INT_MAX) decoded=LoadImageFromMemory("",bytes.data(),static_cast<int>(bytes.size()));
        } else if(uri.find("://")==std::string::npos) {
            std::string path;
            auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
            for(std::size_t i=0;i<uri.size();++i) {
                if(uri[i]=='%' && i+2<uri.size() && hex(uri[i+1])>=0 && hex(uri[i+2])>=0) {path.push_back(static_cast<char>(hex(uri[i+1])*16+hex(uri[i+2]))); i+=2;}
                else path.push_back(uri[i]);
            }
            decoded=LoadImage((std::filesystem::path(fileName).parent_path()/path).string());
        }
    }
    auto result=LoadTextureFromImage(decoded); UnloadImage(decoded);
    if(result.id && texture->sampler) {
        const auto& sampler=*texture->sampler;
        if(sampler.min_filter>=9984) GenTextureMipmaps(&result);
        SetTextureFilter(result,sampler.min_filter>=9984?TextureFilter::Trilinear:sampler.mag_filter==9728?TextureFilter::Point:TextureFilter::Bilinear);
        SetTextureWrap(result,sampler.wrap_s==33071?TextureWrap::Clamp:sampler.wrap_s==33648?TextureWrap::MirrorRepeat:TextureWrap::Repeat);
    }
    return result;
}
Model LoadGLTF(const std::string& fileName) {
    cgltf_options options{};
    cgltf_data* raw=nullptr;
    if(cgltf_parse_file(&options,fileName.c_str(),&raw)!=cgltf_result_success) return {};
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> owner(raw,cgltf_free);
    if(cgltf_load_buffers(&options,raw,fileName.c_str())!=cgltf_result_success || cgltf_validate(raw)!=cgltf_result_success) return {};
    Model model{};
    try {
        if(raw->nodes_count>INT_MAX || raw->materials_count>=INT_MAX || raw->textures_count>INT_MAX)throw std::length_error("glTF exceeds model limits");
        std::vector<bool> visible(raw->nodes_count,raw->scenes_count==0);
        const cgltf_scene* scene=raw->scene?raw->scene:raw->scenes_count?&raw->scenes[0]:nullptr;
        if(scene){
            std::vector<const cgltf_node*> pending;
            for(cgltf_size root=0;root<scene->nodes_count;++root)pending.push_back(scene->nodes[root]);
            while(!pending.empty()){
                const auto* node=pending.back();pending.pop_back();auto index=node-raw->nodes;
                if(visible[index])continue;visible[index]=true;
                for(cgltf_size child=0;child<node->children_count;++child)pending.push_back(node->children[child]);
            }
        }
        std::size_t count=0;
        for(cgltf_size n=0;n<raw->nodes_count;++n) if(visible[n] && raw->nodes[n].mesh) count+=raw->nodes[n].mesh->primitives_count;
        if(count>INT_MAX)throw std::length_error("glTF exceeds mesh limit");
        model.meshes=new Mesh[count]{};
        model.materialCount=static_cast<int>(raw->materials_count)+1;
        model.materials=new Material[model.materialCount]{};
        for(int i=0;i<model.materialCount;++i) model.materials[i]=LoadMaterialDefault();
        model.ownedTextureCount=static_cast<int>(raw->textures_count);
        model.ownedTextures=new Texture2D[model.ownedTextureCount]{};
        auto load=[&](const cgltf_texture_view& view){
            if(!view.texture) return Texture2D{};
            auto index=view.texture-raw->textures;
            auto& texture=model.ownedTextures[index];
            if(!texture.id) texture=LoadGltfTexture(view.texture,fileName);
            return texture;
        };
        // Copy a texture into a material map slot and record its UV set +
        // KHR_texture_transform so the sampled coordinates match the asset.
        auto assign=[&](MaterialMap& map,const cgltf_texture_view& view){
            map.texture=load(view);
            if(!view.texture) return;
            map.uvSet=static_cast<int>(view.texcoord); // TEXCOORD_<n>
            if(view.has_transform) {
                const auto& tr=view.transform;
                map.uvOffset={tr.offset[0],tr.offset[1]};
                map.uvScale={tr.scale[0],tr.scale[1]};
                map.uvRotation=tr.rotation;
                if(tr.has_texcoord) map.uvSet=static_cast<int>(tr.texcoord);
            }
        };
        auto byte=[](float value){return static_cast<unsigned char>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f);};
        for(cgltf_size i=0;i<raw->materials_count;++i) {
            const auto& source=raw->materials[i]; auto& material=model.materials[i+1];
            if(source.has_pbr_metallic_roughness) {
                const auto& pbr=source.pbr_metallic_roughness;
                material.maps[0].color={byte(std::pow(pbr.base_color_factor[0],1/2.2f)),byte(std::pow(pbr.base_color_factor[1],1/2.2f)),byte(std::pow(pbr.base_color_factor[2],1/2.2f)),byte(pbr.base_color_factor[3])};
                assign(material.maps[0],pbr.base_color_texture);
                material.maps[1].value=pbr.metallic_factor; material.maps[3].value=pbr.roughness_factor;
                assign(material.maps[1],pbr.metallic_roughness_texture);
                material.maps[3].texture=material.maps[1].texture;
                material.maps[3].uvSet=material.maps[1].uvSet;
                material.maps[3].uvOffset=material.maps[1].uvOffset;
                material.maps[3].uvScale=material.maps[1].uvScale;
                material.maps[3].uvRotation=material.maps[1].uvRotation;
            }
            assign(material.maps[2],source.normal_texture);
            assign(material.maps[4],source.occlusion_texture);
            assign(material.maps[5],source.emissive_texture);
            material.maps[5].color={byte(source.emissive_factor[0]),byte(source.emissive_factor[1]),byte(source.emissive_factor[2]),255};
            // Alpha handling from the glTF material.
            switch(source.alpha_mode) {
                case cgltf_alpha_mode_mask: material.alphaMode=MaterialAlphaMode::Mask; break;
                case cgltf_alpha_mode_blend: material.alphaMode=MaterialAlphaMode::Blend; break;
                default: material.alphaMode=MaterialAlphaMode::Opaque; break;
            }
            material.alphaCutoff=source.alpha_cutoff;
            // Opt-in lighting preserves the simple raylib-style default API.
            material.lighting=false;
        }
        model.meshMaterial=new int[count]{};
        model.boneCount=static_cast<int>(raw->nodes_count);
        model.bones=new BoneInfo[model.boneCount];
        model.bindPose=new Transform[model.boneCount];
        model.bindMatrices=new Matrix[model.boneCount];
        for(int n=0;n<model.boneCount;++n) {
            model.bones[n]=detail::NodeBone(*raw,raw->nodes[n]);
            model.bindPose[n]=detail::NodePose(raw->nodes[n]);
            float local[16]; cgltf_node_transform_local(&raw->nodes[n],local);
            model.bindMatrices[n]=detail::GltfMatrix(local);
        }
        for(cgltf_size nodeIndex=0;nodeIndex<raw->nodes_count;++nodeIndex) {
            const auto& node=raw->nodes[nodeIndex];
            if(!visible[nodeIndex] || !node.mesh) continue;
            for(cgltf_size p=0;p<node.mesh->primitives_count;++p) {
                const auto& primitive=node.mesh->primitives[p];
                if(primitive.type!=cgltf_primitive_type_triangles) continue;
                const cgltf_accessor *position=nullptr,*uv=nullptr,*uv2=nullptr,*normal=nullptr;
                // Multiple JOINTS_n / WEIGHTS_n sets express more than four
                // influences per vertex; collect every set and clamp to the four
                // strongest below instead of rejecting the model.
                std::vector<const cgltf_accessor*> jointSets,weightSets;
                for(cgltf_size a=0;a<primitive.attributes_count;++a) {
                    const auto& attribute=primitive.attributes[a];
                    if(attribute.type==cgltf_attribute_type_joints) { jointSets.push_back(attribute.data); continue; }
                    if(attribute.type==cgltf_attribute_type_weights) { weightSets.push_back(attribute.data); continue; }
                    if(attribute.type==cgltf_attribute_type_texcoord && attribute.index==1) { uv2=attribute.data; continue; }
                    if(attribute.index!=0) continue;
                    if(attribute.type==cgltf_attribute_type_position) position=attribute.data;
                    if(attribute.type==cgltf_attribute_type_texcoord) uv=attribute.data;
                    if(attribute.type==cgltf_attribute_type_normal) normal=attribute.data;
                }
                if(!position) continue;
                const bool skinned=node.skin && !jointSets.empty() && !weightSets.empty();
                if(node.skin && !skinned)throw std::invalid_argument("Skinned glTF primitive is missing joints or weights");
                const std::size_t vertices=primitive.indices?primitive.indices->count:position->count;
                if(vertices>INT_MAX || vertices%3)throw std::invalid_argument("Invalid glTF triangle count");
                std::vector<float> positions,texcoords,texcoords2,normals;
                std::vector<unsigned short> ids;
                std::vector<float> influenceWeights;
                std::vector<cgltf_size> emittedIndices; // source accessor index per emitted vertex (for morph deltas)
                const std::size_t influenceSets=std::min(jointSets.size(),weightSets.size());
                for(std::size_t i=0;i<vertices;++i) {
                    auto index=primitive.indices?cgltf_accessor_read_index(primitive.indices,i):i;
                    if(index>=position->count) throw std::runtime_error("glTF index exceeds vertex count");
                    emittedIndices.push_back(index);
                    float v[3]{},t[2]{},t2[2]{},n[3]{0,1,0}; float w[4]{1,0,0,0}; cgltf_uint j[4]{};
                    if(!cgltf_accessor_read_float(position,index,v,3)) throw std::runtime_error("glTF position read failed");
                    if(uv) cgltf_accessor_read_float(uv,index,t,2);
                    if(uv2) cgltf_accessor_read_float(uv2,index,t2,2);
                    if(normal) cgltf_accessor_read_float(normal,index,n,3);
                    if(skinned) {
                        // Gather every influence across all JOINTS_n/WEIGHTS_n
                        // sets, then keep the four with the largest weights.
                        struct Influence { cgltf_uint joint; float weight; };
                        std::vector<Influence> influences;
                        for(std::size_t s=0;s<influenceSets;++s) {
                            cgltf_uint sj[4]{}; float sw[4]{};
                            if(!cgltf_accessor_read_uint(jointSets[s],index,sj,4) || !cgltf_accessor_read_float(weightSets[s],index,sw,4)) throw std::runtime_error("glTF skin read failed");
                            for(int k=0;k<4;++k) {
                                if(sj[k]>=node.skin->joints_count || sj[k]>65535 || !std::isfinite(sw[k]) || sw[k]<0) throw std::runtime_error("Invalid glTF joint influence");
                                if(sw[k]>0) influences.push_back({sj[k],sw[k]});
                            }
                        }
                        std::sort(influences.begin(),influences.end(),[](const Influence& a,const Influence& b){return a.weight>b.weight;});
                        if(influences.size()>4) influences.resize(4); // clamp to four strongest
                        float sum=0; for(auto& inf:influences) sum+=inf.weight;
                        for(int k=0;k<4;++k) {
                            if(k<static_cast<int>(influences.size())) { j[k]=influences[k].joint; w[k]=sum>0?influences[k].weight/sum:0; }
                            else { j[k]=0; w[k]=0; }
                        }
                        if(sum<=0) { j[0]=0; w[0]=1; }
                    }
                    positions.insert(positions.end(),v,v+3); texcoords.insert(texcoords.end(),t,t+2); texcoords2.insert(texcoords2.end(),t2,t2+2); normals.insert(normals.end(),n,n+3);
                    for(int k=0;k<4;++k) {ids.push_back(static_cast<unsigned short>(j[k])); influenceWeights.push_back(w[k]);}
                }
                model.meshMaterial[model.meshCount]=primitive.material?static_cast<int>(primitive.material-raw->materials)+1:0;
                auto& mesh=model.meshes[model.meshCount++];
                mesh=MakeMesh(positions,texcoords,normals);
                // Store the secondary UV set when any map references it.
                bool needsUv2=false;
                if(primitive.material) {
                    const auto& m=model.materials[static_cast<int>(primitive.material-raw->materials)+1];
                    for(int s=0;s<6 && m.maps;++s) if(m.maps[s].uvSet==1) needsUv2=true;
                }
                if(needsUv2 && !texcoords2.empty()) {
                    mesh.texcoords2=static_cast<float*>(std::malloc(texcoords2.size()*sizeof(float)));
                    if(!mesh.texcoords2) throw std::bad_alloc();
                    std::memcpy(mesh.texcoords2,texcoords2.data(),texcoords2.size()*sizeof(float));
                }
                if(!normal) for(int triangle=0;triangle<mesh.triangleCount;++triangle) {
                    int base=triangle*9;
                    Vector3 a{mesh.vertices[base],mesh.vertices[base+1],mesh.vertices[base+2]},b{mesh.vertices[base+3],mesh.vertices[base+4],mesh.vertices[base+5]},c{mesh.vertices[base+6],mesh.vertices[base+7],mesh.vertices[base+8]};
                    Vector3 face=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b,a),Vector3Subtract(c,a)));
                    for(int v=0;v<3;++v) {mesh.normals[base+v*3]=face.x;mesh.normals[base+v*3+1]=face.y;mesh.normals[base+v*3+2]=face.z;}
                }
                mesh.boneIds=new unsigned short[ids.size()]; std::copy(ids.begin(),ids.end(),mesh.boneIds);
                mesh.boneWeights=new float[influenceWeights.size()]; std::copy(influenceWeights.begin(),influenceWeights.end(),mesh.boneWeights);
                mesh.boneCount=skinned?static_cast<int>(node.skin->joints_count):1;
                mesh.boneNodes=new int[mesh.boneCount]; mesh.inverseBindMatrices=new Matrix[mesh.boneCount]; mesh.boneMatrices=new Matrix[mesh.boneCount];
                for(int b=0;b<mesh.boneCount;++b) {
                    mesh.boneNodes[b]=skinned?static_cast<int>(node.skin->joints[b]-raw->nodes):static_cast<int>(nodeIndex);
                    if(skinned && node.skin->inverse_bind_matrices) {
                        float inverse[16];
                        if(!cgltf_accessor_read_float(node.skin->inverse_bind_matrices,b,inverse,16)) throw std::runtime_error("glTF inverse bind read failed");
                        mesh.inverseBindMatrices[b]=detail::GltfMatrix(inverse);
                    }
                }

                // glTF morph targets: read POSITION (and NORMAL) deltas for each
                // target and store them flattened to the emitted vertex order so
                // CPU tessellation can blend them by weight.
                if(primitive.targets_count>0) {
                    const int targets=static_cast<int>(primitive.targets_count);
                    const std::size_t vcount=emittedIndices.size();
                    mesh.morphTargetCount=targets;
                    mesh.morphPositions=static_cast<float*>(std::calloc(static_cast<std::size_t>(targets)*vcount*3,sizeof(float)));
                    mesh.morphNormals=static_cast<float*>(std::calloc(static_cast<std::size_t>(targets)*vcount*3,sizeof(float)));
                    mesh.morphWeights=static_cast<float*>(std::calloc(targets,sizeof(float)));
                    if(!mesh.morphPositions||!mesh.morphNormals||!mesh.morphWeights) throw std::bad_alloc();
                    for(int t=0;t<targets;++t) {
                        const auto& target=primitive.targets[t];
                        const cgltf_accessor* mp=nullptr; const cgltf_accessor* mn=nullptr;
                        for(cgltf_size a=0;a<target.attributes_count;++a) {
                            if(target.attributes[a].type==cgltf_attribute_type_position) mp=target.attributes[a].data;
                            else if(target.attributes[a].type==cgltf_attribute_type_normal) mn=target.attributes[a].data;
                        }
                        for(std::size_t vi=0;vi<vcount;++vi) {
                            const cgltf_size srcIndex=emittedIndices[vi];
                            float dp[3]{},dn[3]{};
                            if(mp && srcIndex<mp->count) cgltf_accessor_read_float(mp,srcIndex,dp,3);
                            if(mn && srcIndex<mn->count) cgltf_accessor_read_float(mn,srcIndex,dn,3);
                            const std::size_t off=(static_cast<std::size_t>(t)*vcount+vi)*3;
                            mesh.morphPositions[off]=dp[0]; mesh.morphPositions[off+1]=dp[1]; mesh.morphPositions[off+2]=dp[2];
                            mesh.morphNormals[off]=dn[0]; mesh.morphNormals[off+1]=dn[1]; mesh.morphNormals[off+2]=dn[2];
                        }
                    }
                    // Default weights from the mesh (glTF mesh.weights), if present.
                    for(int t=0;t<targets && t<static_cast<int>(node.mesh->weights_count);++t)
                        mesh.morphWeights[t]=node.mesh->weights[t];
                }
            }
        }
        detail::ApplyPalette(model,model.bindMatrices);
        return model;
    } catch(...) { UnloadModel(model); throw; }
}
} // namespace

Model LoadModel(const std::string& fileName) {
    const std::string lower = ToLower(fileName);
    if (EndsWith(lower, ".obj")) return LoadOBJ(fileName);
    if (EndsWith(lower, ".gltf") || EndsWith(lower, ".glb")) return LoadGLTF(fileName);
    std::fprintf(stderr, "[meowyrender] LoadModel: unsupported format %s\n",
                 fileName.c_str());
    return {};
}

} // namespace meowyrender
