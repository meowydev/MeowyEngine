#include "meowyrender/meowyrender.hpp"
#include "../src/modules/image_pixels.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <limits>
namespace mr=meowyrender;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    for(int f=1;f<=13;++f){
        auto format=static_cast<mr::PixelFormat>(f);
        auto image=mr::GenImageColor(3,2,mr::WHITE);
        mr::ImageFormat(&image,format);
        auto copy=mr::ImageCopy(image);
        Check(std::memcmp(copy.data,image.data,mr::detail::ImageBytes(3,2,format))==0,"format copy");
        mr::ImageDrawPixel(&copy,0,0,mr::BLACK);
        mr::ImageFlipHorizontal(&copy);mr::ImageFlipVertical(&copy);
        Check(mr::GetImageColor(copy,2,1).r==0,"format flip");
        mr::ImageCrop(&copy,{-1,-1,4,3});Check(copy.width==3&&copy.height==2,"negative crop intersection");
        mr::ImageResizeNN(&copy,6,4);Check(mr::GetImageColor(copy,5,3).r==0,"nearest resize");
        mr::ImageResize(&copy,3,2);Check(copy.width==3&&copy.height==2,"filtered resize");
        mr::UnloadImage(copy);mr::UnloadImage(image);
    }
    for(unsigned bits=0;bits<65536;++bits){
        auto value=mr::backend::HalfToFloat(static_cast<unsigned short>(bits));
        auto packed=mr::detail::FloatToHalf(value);
        Check(std::isnan(value)?(packed&0x7c00)==0x7c00&&(packed&1023)!=0:packed==bits,"half float round trip");
    }
    auto hdr=mr::GenImageColor(2,2,mr::WHITE);mr::ImageFormat(&hdr,mr::PixelFormat::Uncompressed_R32G32B32A32);
    for(int i=0;i<4;++i)static_cast<float*>(hdr.data)[i*4]=8.0f;
    mr::ImageFormat(&hdr,mr::PixelFormat::Uncompressed_R16G16B16A16);
    mr::ImageFlipHorizontal(&hdr);mr::ImageResize(&hdr,4,4);
    Check(static_cast<float*>(hdr.data)[0]>7.9f,"HDR must survive half conversion and resize");
    Check(mr::ExportImage(hdr,"image-check.hdr"),"HDR export");
    auto loaded=mr::LoadImage("image-check.hdr");Check(loaded.data&&loaded.format==mr::PixelFormat::Uncompressed_R32G32B32A32,"HDR load format");
    Check(static_cast<float*>(loaded.data)[0]>7.9f,"HDR file range");mr::UnloadImage(loaded);mr::UnloadImage(hdr);
    unsigned char invalid[4]={0,1,2,3};Check(!mr::LoadImageFromMemory(".png",invalid,4).data,"invalid image must not masquerade as a texture");
    Check(!mr::LoadImageFromMemory(".png",nullptr,10).data,"null image input");
    Check(!mr::GenImageColor(-1,2,mr::WHITE).data,"negative dimensions");
    auto tiny=mr::GenImageGradientV(1,1,mr::RED,mr::BLUE);mr::UnloadImage(tiny);
    tiny=mr::GenImageGradientH(1,1,mr::RED,mr::BLUE);mr::UnloadImage(tiny);
    tiny=mr::GenImageChecked(2,2,0,100,mr::WHITE,mr::BLACK);
    mr::ImageDrawRectangle(&tiny,-100,-100,std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),mr::WHITE);
    mr::ImageColorBrightness(&tiny,std::numeric_limits<int>::max());mr::UnloadImage(tiny);
    tiny=mr::GenImageGradientRadial(1,1,1,mr::WHITE,mr::BLACK);mr::UnloadImage(tiny);
    unsigned char compressed[8]={};mr::Image blocks;blocks.data=compressed;blocks.width=4;blocks.height=4;blocks.mipmaps=1;blocks.format=mr::PixelFormat::Compressed_DXT1_RGB;
    auto copied=mr::ImageCopy(blocks);Check(std::memcmp(copied.data,compressed,8)==0,"compressed copy");mr::UnloadImage(copied);
    bool rejected=false;try{mr::ImageFlipHorizontal(&blocks);}catch(const std::invalid_argument&){rejected=true;}Check(rejected,"compressed CPU editing must reject");
    std::puts("PASS: all 13 CPU pixel formats, all half-float patterns, HDR and invalid inputs");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
