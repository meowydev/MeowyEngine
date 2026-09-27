#pragma once
#include "meowyrender/mr_types.hpp"
#include <vector>
#include <cstring>
#include <cmath>
#include <limits>
#include <algorithm>

namespace meowyrender::backend {
inline std::size_t CompressedSize(int width,int height,PixelFormat format) {
    if(width<=0 || height<=0) return 0;
    int block=4,bytes=16;
    switch(format) {
        case PixelFormat::Compressed_DXT1_RGB: case PixelFormat::Compressed_DXT1_RGBA:
        case PixelFormat::Compressed_ETC1_RGB: case PixelFormat::Compressed_ETC2_RGB: bytes=8; break;
        case PixelFormat::Compressed_DXT3_RGBA: case PixelFormat::Compressed_DXT5_RGBA:
        case PixelFormat::Compressed_ETC2_EAC_RGBA: case PixelFormat::Compressed_ASTC_4x4_RGBA: break;
        case PixelFormat::Compressed_ASTC_8x8_RGBA: block=8; break;
        case PixelFormat::Compressed_PVRT_RGB:case PixelFormat::Compressed_PVRT_RGBA:
            return static_cast<size_t>(std::max(width,8))*std::max(height,8)/2;
        default: return 0;
    }
    return ((static_cast<std::size_t>(width)+block-1)/block)*((static_cast<std::size_t>(height)+block-1)/block)*bytes;
}
// Canonical upload storage. Packed integer formats expand to RGBA8; float and
// half-float inputs expand to RGBA32F without clipping HDR values.
struct UploadPixels {
    std::vector<unsigned char> bytes;
    std::vector<float> floats;
    bool valid=false, floating=false;
    const void* data() const { return floating?static_cast<const void*>(floats.data()):bytes.data(); }
    std::size_t stride(int width) const { return static_cast<std::size_t>(width)*(floating?16:4); }
};
inline float HalfToFloat(unsigned short bits) {
    const int exponent=(bits>>10)&31, mantissa=bits&1023;
    float value=exponent==0?std::ldexp(static_cast<float>(mantissa),-24):
        exponent==31?(mantissa?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity()):
        std::ldexp(static_cast<float>(1024+mantissa),exponent-25);
    return (bits&0x8000)?-value:value;
}
inline UploadPixels ConvertUpload(const void* data,int width,int height,PixelFormat format) {
    UploadPixels out;
    if(width<=0 || height<=0) return out;
    const int f=static_cast<int>(format);
    if(f<1 || f>static_cast<int>(PixelFormat::Uncompressed_R16G16B16A16)) return out;
    const std::size_t count=static_cast<std::size_t>(width)*height;
    if(count>std::numeric_limits<std::size_t>::max()/16) return out;
    out.floating=f>=static_cast<int>(PixelFormat::Uncompressed_R32);
    if(out.floating) out.floats.resize(count*4); else out.bytes.resize(count*4);
    out.valid=true;
    if(!data) return out;
    const auto* source=static_cast<const unsigned char*>(data);
    if(out.floating) {
        const bool half=f>=static_cast<int>(PixelFormat::Uncompressed_R16);
        const int kind=(f-static_cast<int>(half?PixelFormat::Uncompressed_R16:PixelFormat::Uncompressed_R32));
        const int channels=kind==0?1:kind==1?3:4;
        for(std::size_t i=0;i<count;++i) {
            float rgba[4]={0,0,0,1};
            for(int c=0;c<channels;++c) {
                if(half) {unsigned short value; std::memcpy(&value,source+(i*channels+c)*2,2); rgba[c]=HalfToFloat(value);}
                else std::memcpy(&rgba[c],source+(i*channels+c)*4,4);
            }
            if(channels==1) rgba[1]=rgba[2]=rgba[0];
            std::memcpy(out.floats.data()+i*4,rgba,sizeof(rgba));
        }
    } else {
        for(std::size_t i=0;i<count;++i) {
            unsigned int r=0,g=0,b=0,a=255;
            switch(format) {
                case PixelFormat::Uncompressed_Grayscale: r=g=b=source[i]; break;
                case PixelFormat::Uncompressed_GrayAlpha: r=g=b=source[i*2]; a=source[i*2+1]; break;
                case PixelFormat::Uncompressed_R8G8B8: r=source[i*3]; g=source[i*3+1]; b=source[i*3+2]; break;
                case PixelFormat::Uncompressed_R8G8B8A8: r=source[i*4]; g=source[i*4+1]; b=source[i*4+2]; a=source[i*4+3]; break;
                default: {
                    unsigned short packed; std::memcpy(&packed,source+i*2,2);
                    if(format==PixelFormat::Uncompressed_R5G6B5) {r=((packed>>11)&31)*255/31; g=((packed>>5)&63)*255/63; b=(packed&31)*255/31;}
                    else if(format==PixelFormat::Uncompressed_R5G5B5A1) {r=((packed>>11)&31)*255/31; g=((packed>>6)&31)*255/31; b=((packed>>1)&31)*255/31; a=(packed&1)*255;}
                    else {r=((packed>>12)&15)*17; g=((packed>>8)&15)*17; b=((packed>>4)&15)*17; a=(packed&15)*17;}
                    break;
                }
            }
            auto* pixel=out.bytes.data()+i*4;
            pixel[0]=r; pixel[1]=g; pixel[2]=b; pixel[3]=a;
        }
    }
    return out;
}
}
