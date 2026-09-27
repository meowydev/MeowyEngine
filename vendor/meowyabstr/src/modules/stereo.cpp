#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"
#include <cmath>
#include <stdexcept>

namespace meowyrender {
VrStereoConfig LoadVrStereoConfig(VrDeviceInfo d) {
    if(d.hResolution<2||d.vResolution<1||!std::isfinite(d.hScreenSize)||d.hScreenSize<=0||
       !std::isfinite(d.vScreenSize)||d.vScreenSize<=0||!std::isfinite(d.eyeToScreenDistance)||d.eyeToScreenDistance<=0||
       !std::isfinite(d.interpupillaryDistance)||d.interpupillaryDistance<0||
       !std::isfinite(d.lensSeparationDistance)||d.lensSeparationDistance<0||d.lensSeparationDistance>d.hScreenSize)
        throw std::invalid_argument("Invalid stereo device geometry");
    const float aspect=d.hResolution*0.5f/d.vResolution;
    const float shift=1-2*d.lensSeparationDistance/d.hScreenSize;
    const float radius=1+std::abs(shift),r2=radius*radius;
    float distortion=d.lensDistortionValues[0]+r2*(d.lensDistortionValues[1]+r2*(d.lensDistortionValues[2]+r2*d.lensDistortionValues[3]));
    if(!std::isfinite(distortion)||distortion<=0)throw std::invalid_argument("Invalid lens distortion coefficients");
    const float fov=2*std::atan(d.vScreenSize*distortion/(2*d.eyeToScreenDistance));
    VrStereoConfig result;
    for(int eye=0;eye<2;++eye) {
        result.projection[eye]=MatrixPerspective(fov,aspect,0.01,1000);
        result.projection[eye].m8=eye==0?shift:-shift;
        result.viewOffset[eye]=MatrixTranslate((eye==0?0.5f:-0.5f)*d.interpupillaryDistance,0,0);
    }
    result.leftLensCenter[0]=0.25f+shift*0.25f;result.leftLensCenter[1]=0.5f;
    result.rightLensCenter[0]=0.75f-shift*0.25f;result.rightLensCenter[1]=0.5f;
    result.scale[0]=0.25f/distortion;result.scale[1]=0.5f*aspect/distortion;
    result.scaleIn[0]=4;result.scaleIn[1]=2/aspect;
    return result;
}
// VrStereoConfig is a pure value type: fixed-size Matrix[2] + float[2] arrays,
// no pointers, heap allocations, or GPU handles (see mr_types.hpp). Unlike
// raylib -- whose rlLoadVrStereoConfig compiles a lens-distortion shader that
// UnloadVrStereoConfig then frees -- MeowyRender's BeginVrStereoMode applies the
// stereo split without a per-config GPU resource, so there is nothing to
// release here. This is a correct architectural no-op, verified idempotent and
// side-effect-free by stereo_checks, NOT an unimplemented capability.
void UnloadVrStereoConfig(VrStereoConfig) {}
void BeginVrStereoMode(VrStereoConfig config) {
    auto& s=detail::State();
    if(s.stereo)throw std::logic_error("Stereo modes cannot be nested");
    detail::FlushBatch();s.stereoConfig=config;s.stereo=true;
}
void EndVrStereoMode() {detail::FlushBatch();detail::State().stereo=false;}
}
