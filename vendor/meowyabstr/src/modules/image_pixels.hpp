#pragma once
#include "meowyrender/mr_types.hpp"
#include "../backend/pixel_conversion.hpp"
#include <bit>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace meowyrender::detail {
inline int PixelBytes(PixelFormat format) {
    constexpr int sizes[]={0,1,2,2,3,2,2,4,4,12,16,2,6,8};int value=static_cast<int>(format);
    return value>0&&value<14?sizes[value]:0;
}
inline size_t ImageBytes(int width,int height,PixelFormat format) {
    if(width<=0||height<=0)return 0;
    int bytes=PixelBytes(format);
    if(!bytes)return backend::CompressedSize(width,height,format);
    size_t count=static_cast<size_t>(width)*height;
    if(count>static_cast<size_t>(std::numeric_limits<int>::max())||count>std::numeric_limits<size_t>::max()/bytes)throw std::length_error("Image dimensions exceed addressable storage");
    return count*bytes;
}
inline unsigned short FloatToHalf(float value) {
    uint32_t bits=std::bit_cast<uint32_t>(value),sign=(bits>>16)&0x8000,mantissa=bits&0x7fffff;
    int exponent=static_cast<int>((bits>>23)&255)-127+15;
    if(((bits>>23)&255)==255)return static_cast<unsigned short>(sign|0x7c00|(mantissa?0x200:0));
    if(exponent>=31)return static_cast<unsigned short>(sign|0x7c00);
    if(exponent<=0) {
        if(exponent<-10)return static_cast<unsigned short>(sign);
        mantissa|=0x800000;int shift=14-exponent;uint32_t half=mantissa>>shift,rest=mantissa&((1u<<shift)-1),mid=1u<<(shift-1);
        return static_cast<unsigned short>(sign|half+((rest>mid||(rest==mid&&(half&1)))?1:0));
    }
    mantissa+=0xfff+((mantissa>>13)&1);
    if(mantissa&0x800000){mantissa=0;++exponent;}
    return static_cast<unsigned short>(sign|(exponent>=31?0x7c00:static_cast<uint32_t>(exponent<<10)|(mantissa>>13)));
}
inline Vector4 ReadPixel(const void* address,PixelFormat format) {
    auto* p=static_cast<const unsigned char*>(address);int f=static_cast<int>(format);
    Vector4 out{0,0,0,1};float* values=&out.x;
    if(f>=8&&f<=13) {
        bool half=f>=11;int kind=f-(half?11:8),channels=kind==0?1:kind==1?3:4;
        for(int i=0;i<channels;++i)if(half){unsigned short bits;std::memcpy(&bits,p+i*2,2);values[i]=backend::HalfToFloat(bits);}else std::memcpy(values+i,p+i*4,4);
        if(channels==1)out.y=out.z=out.x;return out;
    }
    unsigned int r=0,g=0,b=0,a=255;unsigned short packed=0;
    if(f==3||f==5||f==6)std::memcpy(&packed,p,2);
    switch(f) {
        case 1:r=g=b=p[0];break;case 2:r=g=b=p[0];a=p[1];break;
        case 3:r=((packed>>11)&31)*255/31;g=((packed>>5)&63)*255/63;b=(packed&31)*255/31;break;
        case 4:r=p[0];g=p[1];b=p[2];break;
        case 5:r=((packed>>11)&31)*255/31;g=((packed>>6)&31)*255/31;b=((packed>>1)&31)*255/31;a=(packed&1)*255;break;
        case 6:r=((packed>>12)&15)*17;g=((packed>>8)&15)*17;b=((packed>>4)&15)*17;a=(packed&15)*17;break;
        case 7:r=p[0];g=p[1];b=p[2];a=p[3];break;
        default:throw std::invalid_argument("CPU pixel operations require an uncompressed image");
    }
    return {r/255.0f,g/255.0f,b/255.0f,a/255.0f};
}
inline unsigned char Byte(float value){return static_cast<unsigned char>(std::isnan(value)?0:std::clamp(value,0.0f,1.0f)*255+0.5f);}
inline void WritePixel(void* address,PixelFormat format,Vector4 color) {
    auto* p=static_cast<unsigned char*>(address);int f=static_cast<int>(format);
    if(f>=8&&f<=13) {
        bool half=f>=11;int kind=f-(half?11:8),channels=kind==0?1:kind==1?3:4;
        for(int i=0;i<channels;++i)if(half){auto bits=FloatToHalf((&color.x)[i]);std::memcpy(p+i*2,&bits,2);}else std::memcpy(p+i*4,&(&color.x)[i],4);
        return;
    }
    unsigned int r=Byte(color.x),g=Byte(color.y),b=Byte(color.z),a=Byte(color.w);unsigned short packed=0;
    switch(f) {
        case 1:p[0]=Byte(color.x*0.299f+color.y*0.587f+color.z*0.114f);break;
        case 2:p[0]=Byte(color.x*0.299f+color.y*0.587f+color.z*0.114f);p[1]=a;break;
        case 3:packed=((r*31+127)/255)<<11|((g*63+127)/255)<<5|((b*31+127)/255);break;
        case 4:p[0]=r;p[1]=g;p[2]=b;break;
        case 5:packed=((r*31+127)/255)<<11|((g*31+127)/255)<<6|((b*31+127)/255)<<1|(a>=128);break;
        case 6:packed=((r*15+127)/255)<<12|((g*15+127)/255)<<8|((b*15+127)/255)<<4|((a*15+127)/255);break;
        case 7:p[0]=r;p[1]=g;p[2]=b;p[3]=a;break;
        default:throw std::invalid_argument("CPU pixel operations cannot encode compressed formats");
    }
    if(f==3||f==5||f==6)std::memcpy(p,&packed,2);
}
}
