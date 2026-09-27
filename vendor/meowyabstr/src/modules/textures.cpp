// meowyrender - src/modules/textures.cpp
// Image (CPU) + Texture (GPU): real file loading via stb_image, procedural
// generators, pixel-format conversion, and image manipulation.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"
#include "image_pixels.hpp"
#include <memory>
#include <cctype>

#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

namespace meowyrender {

using detail::PushVertex;
using detail::SetBatchState;
using backend::DrawMode;

// ---------------------------------------------------------------------------
// Pixel-format helpers
// ---------------------------------------------------------------------------
namespace {

int RequirePixelBytes(PixelFormat format) {
    int bytes=detail::PixelBytes(format);
    if (!bytes) throw std::invalid_argument("CPU image editing requires an uncompressed format");
    return bytes;
}
Color ReadColor(const Image& img, int x, int y) {
    if (!img.data || x<0 || y<0 || x>=img.width || y>=img.height) return BLANK;
    int bytes=RequirePixelBytes(img.format);
    auto c=detail::ReadPixel(static_cast<const unsigned char*>(img.data)+(static_cast<size_t>(y)*img.width+x)*bytes,img.format);
    return {detail::Byte(c.x),detail::Byte(c.y),detail::Byte(c.z),detail::Byte(c.w)};
}
void WriteColor(Image& img, int x, int y, Color c) {
    if (!img.data || x<0 || y<0 || x>=img.width || y>=img.height) return;
    int bytes=RequirePixelBytes(img.format);
    detail::WritePixel(static_cast<unsigned char*>(img.data)+(static_cast<size_t>(y)*img.width+x)*bytes,img.format,{c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f});
    img.mipmaps=1;
}
Image AllocImage(int width,int height,PixelFormat format) {
    if(width<=0 || height<=0) return {};
    RequirePixelBytes(format);
    Image img; img.width=width;img.height=height;img.format=format;img.mipmaps=1;
    img.data=std::calloc(detail::ImageBytes(width,height,format),1);
    if(!img.data)throw std::bad_alloc();
    return img;
}
Image AllocRGBA(int width,int height) { return AllocImage(width,height,PixelFormat::Uncompressed_R8G8B8A8); }

} // namespace

// ---------------------------------------------------------------------------
// Image loading
// ---------------------------------------------------------------------------
Image LoadImage(const std::string& fileName) {
    Image img; int channels=0;
    bool hdr=stbi_is_hdr(fileName.c_str());
    img.data=hdr?static_cast<void*>(stbi_loadf(fileName.c_str(),&img.width,&img.height,&channels,4)):
                 static_cast<void*>(stbi_load(fileName.c_str(),&img.width,&img.height,&channels,4));
    if(!img.data){std::fprintf(stderr,"[meowyrender] LoadImage failed: %s\n",fileName.c_str());return {};}
    img.format=hdr?PixelFormat::Uncompressed_R32G32B32A32:PixelFormat::Uncompressed_R8G8B8A8;
    img.mipmaps=1;return img;
}
Image LoadImageFromMemory(const std::string&,const unsigned char* data,int dataSize) {
    if(!data || dataSize<=0)return {};
    Image img;int channels=0;bool hdr=stbi_is_hdr_from_memory(data,dataSize);
    img.data=hdr?static_cast<void*>(stbi_loadf_from_memory(data,dataSize,&img.width,&img.height,&channels,4)):
                 static_cast<void*>(stbi_load_from_memory(data,dataSize,&img.width,&img.height,&channels,4));
    if(!img.data)return {};
    img.format=hdr?PixelFormat::Uncompressed_R32G32B32A32:PixelFormat::Uncompressed_R8G8B8A8;
    img.mipmaps=1;return img;
}
bool ExportImage(Image image,const std::string& fileName) {
    if(!image.data || image.width<=0 || image.height<=0 || !detail::PixelBytes(image.format))return false;
    std::string extension=fileName.substr(fileName.find_last_of('.')==std::string::npos?fileName.size():fileName.find_last_of('.'));
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    Image copy=ImageCopy(image);
    try{ImageFormat(&copy,extension==".hdr"?PixelFormat::Uncompressed_R32G32B32A32:PixelFormat::Uncompressed_R8G8B8A8);}
    catch(...){UnloadImage(copy);throw;}
    std::unique_ptr<void,decltype(&std::free)> data(copy.data,&std::free);
    if(extension==".hdr")return stbi_write_hdr(fileName.c_str(),copy.width,copy.height,4,static_cast<float*>(copy.data))!=0;
    if(extension==".bmp")return stbi_write_bmp(fileName.c_str(),copy.width,copy.height,4,copy.data)!=0;
    if(extension==".tga")return stbi_write_tga(fileName.c_str(),copy.width,copy.height,4,copy.data)!=0;
    if(extension==".jpg" || extension==".jpeg")return stbi_write_jpg(fileName.c_str(),copy.width,copy.height,4,copy.data,90)!=0;
    if(copy.width>std::numeric_limits<int>::max()/4)return false;
    return stbi_write_png(fileName.c_str(),copy.width,copy.height,4,copy.data,copy.width*4)!=0;
}
void UnloadImage(Image image) { std::free(image.data); }
Image ImageCopy(Image image) {
    if(!image.data || image.width<=0 || image.height<=0)return {};
    size_t bytes=0;int w=image.width,h=image.height;
    if(image.mipmaps<1 || image.mipmaps>32)throw std::invalid_argument("Invalid image mip count");
    for(int level=0;level<image.mipmaps;++level) {
        size_t size=detail::ImageBytes(w,h,image.format);
        if(!size || size>std::numeric_limits<size_t>::max()-bytes)throw std::length_error("Invalid image size");
        bytes+=size;
        if(level+1<image.mipmaps && w==1 && h==1)throw std::invalid_argument("Image mip count exceeds dimensions");
        w=std::max(1,w/2);h=std::max(1,h/2);
    }
    Image copy=image;copy.data=std::malloc(bytes);
    if(!copy.data)throw std::bad_alloc();
    std::memcpy(copy.data,image.data,bytes);return copy;
}

// ---------------------------------------------------------------------------
// Image generation
// ---------------------------------------------------------------------------
Image GenImageColor(int width, int height, Color color) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    auto* p = static_cast<unsigned char*>(img.data);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        p[i * 4 + 0] = color.r; p[i * 4 + 1] = color.g;
        p[i * 4 + 2] = color.b; p[i * 4 + 3] = color.a;
    }
    return img;
}

Image GenImageGradientV(int width, int height, Color top, Color bottom) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    for (int y = 0; y < height; ++y) {
        const Color c = ColorLerp(top, bottom, static_cast<float>(y) / std::max(1, height - 1));
        for (int x = 0; x < width; ++x) WriteColor(img, x, y, c);
    }
    return img;
}

Image GenImageGradientH(int width, int height, Color left, Color right) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    for (int x = 0; x < width; ++x) {
        const Color c = ColorLerp(left, right, static_cast<float>(x) / std::max(1, width - 1));
        for (int y = 0; y < height; ++y) WriteColor(img, x, y, c);
    }
    return img;
}

Image GenImageGradientRadial(int width, int height, float density,
                             Color inner, Color outer) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    density=std::isfinite(density)?std::clamp(density,0.0f,1.0f):0.0f;
    const float cx = width / 2.0f, cy = height / 2.0f;
    const float radius = (width < height ? width : height) / 2.0f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float dist = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            float f = (dist - radius * density) / std::max(0.000001f, radius * (1.0f - density));
            f = Clamp(f, 0.0f, 1.0f);
            WriteColor(img, x, y, ColorLerp(inner, outer, f));
        }
    }
    return img;
}

Image GenImageChecked(int width, int height, int checksX, int checksY,
                      Color col1, Color col2) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int cx = x / std::max(1, width / std::max(1, checksX));
            const int cy = y / std::max(1, height / std::max(1, checksY));
            WriteColor(img, x, y, ((cx + cy) % 2 == 0) ? col1 : col2);
        }
    }
    return img;
}

Image GenImageWhiteNoise(int width, int height, float factor) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool on = (std::rand() / static_cast<float>(RAND_MAX)) < factor;
            WriteColor(img, x, y, on ? WHITE : BLACK);
        }
    }
    return img;
}

// ---------------------------------------------------------------------------
// Image manipulation
// ---------------------------------------------------------------------------
void ImageFormat(Image* image,PixelFormat format) {
    if(!image || !image->data || image->format==format)return;
    int srcBytes=RequirePixelBytes(image->format),dstBytes=RequirePixelBytes(format);
    Image dst=AllocImage(image->width,image->height,format);
    auto* src=static_cast<const unsigned char*>(image->data);auto* out=static_cast<unsigned char*>(dst.data);
    for(size_t i=0,n=static_cast<size_t>(image->width)*image->height;i<n;++i)
        detail::WritePixel(out+i*dstBytes,format,detail::ReadPixel(src+i*srcBytes,image->format));
    UnloadImage(*image);*image=dst;
}
void ImageResize(Image* image,int width,int height) {
    if(!image || !image->data || width<=0 || height<=0)return;
    int f=static_cast<int>(image->format);bool floating=f>=8&&f<=13;
    ImageFormat(image,floating?PixelFormat::Uncompressed_R32G32B32A32:PixelFormat::Uncompressed_R8G8B8A8);
    Image dst=AllocImage(width,height,image->format);
    bool ok=floating?stbir_resize_float_linear(static_cast<const float*>(image->data),image->width,image->height,0,static_cast<float*>(dst.data),width,height,0,STBIR_RGBA)!=nullptr:
        stbir_resize_uint8_linear(static_cast<const unsigned char*>(image->data),image->width,image->height,0,static_cast<unsigned char*>(dst.data),width,height,0,STBIR_RGBA)!=nullptr;
    if(!ok){UnloadImage(dst);throw std::runtime_error("Image resize failed");}
    UnloadImage(*image);*image=dst;
}
void ImageResizeNN(Image* image,int width,int height) {
    if(!image || !image->data || width<=0 || height<=0)return;
    int bytes=RequirePixelBytes(image->format);
    Image dst=AllocImage(width,height,image->format);
    auto* src=static_cast<const unsigned char*>(image->data);auto* out=static_cast<unsigned char*>(dst.data);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        size_t sx=static_cast<size_t>(x)*image->width/width,sy=static_cast<size_t>(y)*image->height/height;
        std::memcpy(out+(static_cast<size_t>(y)*width+x)*bytes,src+(sy*image->width+sx)*bytes,bytes);
    }
    UnloadImage(*image);*image=dst;
}
void ImageCrop(Image* image,Rectangle crop) {
    if(!image || !image->data || !std::isfinite(crop.x) || !std::isfinite(crop.y) || !std::isfinite(crop.width) || !std::isfinite(crop.height) || crop.width<=0 || crop.height<=0)return;
    int bytes=RequirePixelBytes(image->format);
    int left=static_cast<int>(std::clamp<double>(crop.x,0,image->width)),top=static_cast<int>(std::clamp<double>(crop.y,0,image->height));
    int right=static_cast<int>(std::clamp<double>(static_cast<double>(crop.x)+crop.width,0,image->width)),bottom=static_cast<int>(std::clamp<double>(static_cast<double>(crop.y)+crop.height,0,image->height));
    if(right<=left || bottom<=top)return;
    Image dst=AllocImage(right-left,bottom-top,image->format);
    for(int y=0;y<dst.height;++y)
        std::memcpy(static_cast<unsigned char*>(dst.data)+static_cast<size_t>(y)*dst.width*bytes,
            static_cast<const unsigned char*>(image->data)+(static_cast<size_t>(top+y)*image->width+left)*bytes,static_cast<size_t>(dst.width)*bytes);
    UnloadImage(*image);*image=dst;
}
void ImageFlipVertical(Image* image) {
    if(!image || !image->data)return;
    size_t row=static_cast<size_t>(image->width)*RequirePixelBytes(image->format);
    auto* p=static_cast<unsigned char*>(image->data);std::vector<unsigned char> tmp(row);
    for(int y=0;y<image->height/2;++y){
        auto* a=p+y*row;auto* b=p+(image->height-1-y)*row;
        std::memcpy(tmp.data(),a,row);std::memcpy(a,b,row);std::memcpy(b,tmp.data(),row);
    }
    image->mipmaps=1;
}
void ImageFlipHorizontal(Image* image) {
    if(!image || !image->data)return;
    int bytes=RequirePixelBytes(image->format);auto* p=static_cast<unsigned char*>(image->data);
    for(int y=0;y<image->height;++y)for(int x=0;x<image->width/2;++x)
        for(int b=0;b<bytes;++b)std::swap(p[(static_cast<size_t>(y)*image->width+x)*bytes+b],p[(static_cast<size_t>(y)*image->width+image->width-1-x)*bytes+b]);
    image->mipmaps=1;
}

void ImageColorTint(Image* image, Color color) {
    if (!image || !image->data) return;
    ImageFormat(image, PixelFormat::Uncompressed_R8G8B8A8);
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            Color c = ReadColor(*image, x, y);
            c.r = static_cast<unsigned char>(c.r * color.r / 255);
            c.g = static_cast<unsigned char>(c.g * color.g / 255);
            c.b = static_cast<unsigned char>(c.b * color.b / 255);
            c.a = static_cast<unsigned char>(c.a * color.a / 255);
            WriteColor(*image, x, y, c);
        }
}

void ImageColorInvert(Image* image) {
    if (!image || !image->data) return;
    ImageFormat(image, PixelFormat::Uncompressed_R8G8B8A8);
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            Color c = ReadColor(*image, x, y);
            c.r = 255 - c.r; c.g = 255 - c.g; c.b = 255 - c.b;
            WriteColor(*image, x, y, c);
        }
}

void ImageColorGrayscale(Image* image) {
    if (!image || !image->data) return;
    ImageFormat(image, PixelFormat::Uncompressed_R8G8B8A8);
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            Color c = ReadColor(*image, x, y);
            const auto g = static_cast<unsigned char>(
                0.299f * c.r + 0.587f * c.g + 0.114f * c.b);
            WriteColor(*image, x, y, {g, g, g, c.a});
        }
}

void ImageColorBrightness(Image* image, int brightness) {
    if (!image || !image->data) return;
    ImageFormat(image, PixelFormat::Uncompressed_R8G8B8A8);
    brightness=std::clamp(brightness,-255,255);
    auto clampb = [](int v) { return static_cast<unsigned char>(std::clamp(v, 0, 255)); };
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            Color c = ReadColor(*image, x, y);
            WriteColor(*image, x, y,
                       {clampb(c.r + brightness), clampb(c.g + brightness),
                        clampb(c.b + brightness), c.a});
        }
}

Color GetImageColor(Image image, int x, int y) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return BLANK;
    return ReadColor(image, x, y);
}

void ImageDrawPixel(Image* image, int x, int y, Color color) {
    if (!image || x < 0 || y < 0 || x >= image->width || y >= image->height) return;
    WriteColor(*image, x, y, color);
}

void ImageDrawRectangle(Image* image, int x, int y, int w, int h, Color color) {
    if (!image || !image->data) return;
    const int right=static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(x)+w,0,image->width));
    const int bottom=static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(y)+h,0,image->height));
    for (int j = std::max(0,y); j < bottom; ++j)
        for (int i = std::max(0,x); i < right; ++i)
            ImageDrawPixel(image, i, j, color);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
Texture2D LoadTextureFromImage(Image image) {
    Texture2D tex;
    tex.width = image.width;
    tex.height = image.height;
    tex.format = image.format;
    auto& s = detail::State();
    if (s.backend)
        tex.id = s.backend->CreateTexture(image.data, image.width, image.height,
                                          image.format);
    return tex;
}
bool IsTextureFormatSupported(PixelFormat format) {
    auto& state=detail::State();
    return state.backend && state.backend->SupportsTextureFormat(format);
}

Texture2D LoadTexture(const std::string& fileName) {
    Image img = LoadImage(fileName);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

void UnloadTexture(Texture2D texture) {
    detail::FlushBatch();
    auto& s = detail::State();
    if (s.backend && texture.id) s.backend->DestroyTexture(texture.id);
}

void UpdateTexture(Texture2D texture, const void* pixels) {
    detail::FlushBatch();
    auto& s = detail::State();
    if (s.backend && texture.id)
        s.backend->UpdateTexture(texture.id, texture.width, texture.height,
                                 texture.format, pixels);
}

void GenTextureMipmaps(Texture2D* texture) {
    detail::FlushBatch();
    auto& s = detail::State();
    if (s.backend && texture && texture->id) {
        const int levels=s.backend->GenTextureMipmaps(texture->id);
        if(levels>0) texture->mipmaps=levels;
    }
}

void SetTextureFilter(Texture2D texture, TextureFilter filter) {
    detail::FlushBatch();
    auto& s = detail::State();
    if (s.backend && texture.id)
        s.backend->SetTextureFilter(texture.id, static_cast<int>(filter));
}

void SetTextureWrap(Texture2D texture, TextureWrap wrap) {
    detail::FlushBatch();
    auto& s = detail::State();
    if (s.backend && texture.id)
        s.backend->SetTextureWrap(texture.id, static_cast<int>(wrap));
}

Image LoadImageFromTexture(Texture2D texture) {
    auto& s = detail::State();
    detail::FlushBatch();
    return s.backend && texture.id ? s.backend->ReadTexture(texture.id) : Image{};
}

// ---------------------------------------------------------------------------
// Textured quad drawing
// ---------------------------------------------------------------------------
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                    Vector2 origin, float rotation, Color tint) {
    if (texture.id == 0) return;
    SetBatchState(DrawMode::Triangles, texture.id);

    const float u0 = source.x / texture.width;
    const float v0 = source.y / texture.height;
    const float u1 = (source.x + source.width) / texture.width;
    const float v1 = (source.y + source.height) / texture.height;

    const float rad = rotation * DEG2RAD;
    const float c = std::cos(rad), s = std::sin(rad);
    auto rot = [&](float px, float py) -> Vector2 {
        const float dx = px - origin.x, dy = py - origin.y;
        return {dest.x + dx * c - dy * s, dest.y + dx * s + dy * c};
    };
    const Vector2 tl = rot(0, 0), tr = rot(dest.width, 0);
    const Vector2 br = rot(dest.width, dest.height), bl = rot(0, dest.height);

    PushVertex(tl.x, tl.y, u0, v0, tint);
    PushVertex(bl.x, bl.y, u0, v1, tint);
    PushVertex(br.x, br.y, u1, v1, tint);
    PushVertex(tl.x, tl.y, u0, v0, tint);
    PushVertex(br.x, br.y, u1, v1, tint);
    PushVertex(tr.x, tr.y, u1, v0, tint);
}

void DrawTextureRec(Texture2D texture, Rectangle source, Vector2 position, Color tint) {
    DrawTexturePro(texture, source,
                   {position.x, position.y, std::fabs(source.width), std::fabs(source.height)},
                   {0, 0}, 0.0f, tint);
}

void DrawTextureEx(Texture2D texture, Vector2 position, float rotation,
                   float scale, Color tint) {
    const Rectangle src = {0, 0, static_cast<float>(texture.width),
                           static_cast<float>(texture.height)};
    const Rectangle dst = {position.x, position.y, texture.width * scale,
                           texture.height * scale};
    DrawTexturePro(texture, src, dst, {0, 0}, rotation, tint);
}

void DrawTextureV(Texture2D texture, Vector2 position, Color tint) {
    DrawTextureEx(texture, position, 0.0f, 1.0f, tint);
}
void DrawTexture(Texture2D texture, int x, int y, Color tint) {
    DrawTextureV(texture, {static_cast<float>(x), static_cast<float>(y)}, tint);
}

} // namespace meowyrender
