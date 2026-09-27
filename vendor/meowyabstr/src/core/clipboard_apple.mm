// meowyrender - src/core/clipboard_apple.mm
// Real clipboard-image read on Apple platforms. macOS uses NSPasteboard, iOS /
// visionOS use UIPasteboard. Returns malloc'd RGBA8 pixels (freed by
// UnloadImage). On non-Apple platforms this TU is not compiled and
// GetClipboardImage() falls back to an empty image (GLFW exposes no image
// clipboard). Declared in rcore_events.cpp as detail::PlatformClipboardImage().
#include "meowyrender/mr_types.hpp"

#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <Foundation/Foundation.h>

namespace meowyrender::detail {

// Render a CGImage into a fresh RGBA8 (non-premultiplied, top-left origin)
// buffer and wrap it in an Image. Returns an empty Image on any failure.
static Image ImageFromCGImage(CGImageRef cg) {
    if (!cg) return Image{};
    const size_t w = CGImageGetWidth(cg), h = CGImageGetHeight(cg);
    if (w == 0 || h == 0 || w > 32768 || h > 32768) return Image{};
    void* pixels = std::calloc(w * h, 4);
    if (!pixels) return Image{};
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    // kCGImageAlphaPremultipliedLast + byte order default gives RGBA8 rows.
    const uint32_t bitmapInfo = (uint32_t)kCGImageAlphaPremultipliedLast | (uint32_t)kCGBitmapByteOrder32Big;
    CGContextRef ctx = CGBitmapContextCreate(pixels, w, h, 8, w * 4, space, bitmapInfo);
    CGColorSpaceRelease(space);
    if (!ctx) { std::free(pixels); return Image{}; }
    CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), cg);
    CGContextRelease(ctx);
    Image img{};
    img.data = pixels;
    img.width = (int)w;
    img.height = (int)h;
    img.mipmaps = 1;
    img.format = PixelFormat::Uncompressed_R8G8B8A8;
    return img;
}

// Decode raw encoded image bytes (TIFF/PNG/...) with ImageIO. This is a pure
// CoreGraphics/ImageIO path that does NOT require NSApplication/AppKit UI
// bootstrap, so it is safe to call from a headless console process.
static Image ImageFromEncodedData(NSData* data) {
    if (!data || data.length == 0) return Image{};
    CGImageSourceRef src = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
    if (!src) return Image{};
    Image out{};
    if (CGImageSourceGetCount(src) > 0) {
        CGImageRef cg = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
        out = ImageFromCGImage(cg);
        if (cg) CGImageRelease(cg);
    }
    CFRelease(src);
    return out;
}

Image PlatformClipboardImage() {
    // An autorelease pool is required: this may be called from a plain console
    // tool with no ambient pool, and the pasteboard APIs autorelease.
    @autoreleasepool {
#if TARGET_OS_OSX
        // NSPasteboard needs the AppKit machinery bootstrapped. In a GUI app the
        // window already did this; in a headless/console process it has not, and
        // touching the pasteboard would crash. NSApplicationLoad() is a safe,
        // idempotent way to initialize just enough of AppKit (no window) so the
        // pasteboard is usable from either context.
        NSApplicationLoad();
        // Read the encoded image DATA off the pasteboard and decode via ImageIO,
        // avoiding NSImage/AppKit UI paths that can crash without NSApplication.
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        if (!pb) return Image{};
        // Prefer TIFF: it is the native macOS pasteboard image type and decodes
        // through a different ImageIO plugin than PNG. Recent macOS has an
        // ImageIO PNG-plugin fault (PNGReadPlugin::InitializePluginData) that can
        // SIGBUS in some link configurations, so PNG is only a last resort.
        NSData* data = [pb dataForType:NSPasteboardTypeTIFF];
        if (!data) data = [pb dataForType:NSPasteboardTypePNG];
        return ImageFromEncodedData(data);
#else
        UIImage* uiImage = [UIPasteboard generalPasteboard].image;
        if (!uiImage) return Image{};
        return ImageFromCGImage(uiImage.CGImage);
#endif
    }
}

} // namespace meowyrender::detail

#endif // __APPLE__
