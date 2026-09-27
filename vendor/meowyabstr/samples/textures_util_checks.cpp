// meowyrender - samples/textures_util_checks.cpp
// Deterministic CPU-side tests for rtextures color/pixel/image utilities.
// No graphics context required.
#include "meowyrender/meowyrender.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace mr = meowyrender;
static void Check(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }

int main() {
    try {
        // --- Color helpers ---
        mr::Color red{255, 0, 0, 255};
        Check(mr::ColorToInt(red) == (int)0xFF0000FF, "ColorToInt");
        Check(mr::ColorIsEqual(mr::GetColor(0xFF0000FF), red), "GetColor round trip");
        Check(mr::ColorIsEqual(mr::ColorTint(mr::WHITE, red), red), "ColorTint white*red");
        {
            mr::Vector3 hsv = mr::ColorToHSV(red);
            Check(std::fabs(hsv.x - 0.0f) < 1.0f && hsv.y > 0.99f && hsv.z > 0.99f, "ColorToHSV red");
            mr::Color back = mr::ColorFromHSV(hsv.x, hsv.y, hsv.z);
            Check(back.r > 250 && back.g < 5 && back.b < 5, "ColorFromHSV red");
        }
        {
            // Alpha blend: opaque src fully covers dst.
            mr::Color blended = mr::ColorAlphaBlend(mr::BLUE, red, mr::WHITE);
            Check(blended.r > 250 && blended.b < 5, "ColorAlphaBlend opaque");
        }
        Check(mr::ColorBrightness(mr::BLACK, 1.0f).r == 255, "ColorBrightness max");
        Check(mr::ColorBrightness(mr::WHITE, -1.0f).r == 0, "ColorBrightness min");

        // --- Pixel data ---
        Check(mr::GetPixelDataSize(4, 4, (int)mr::PixelFormat::Uncompressed_R8G8B8A8) == 64, "GetPixelDataSize rgba8");
        {
            unsigned char buf[4] = {0};
            mr::SetPixelColor(buf, red, (int)mr::PixelFormat::Uncompressed_R8G8B8A8);
            mr::Color c = mr::GetPixelColor(buf, (int)mr::PixelFormat::Uncompressed_R8G8B8A8);
            Check(mr::ColorIsEqual(c, red), "SetPixelColor/GetPixelColor round trip");
        }

        // --- Image validity ---
        {
            mr::Image img = mr::GenImageColor(8, 8, red);
            Check(mr::IsImageValid(img), "IsImageValid true");
            mr::Image empty{};
            Check(!mr::IsImageValid(empty), "IsImageValid false");
            mr::UnloadImage(img);
        }

        // --- Image color arrays / palette ---
        {
            mr::Image img = mr::GenImageColor(4, 4, red);
            mr::Color* colors = mr::LoadImageColors(img);
            Check(colors && mr::ColorIsEqual(colors[0], red) && mr::ColorIsEqual(colors[15], red), "LoadImageColors");
            mr::UnloadImageColors(colors);
            int n = 0;
            mr::Color* pal = mr::LoadImagePalette(img, 16, &n);
            Check(n == 1 && mr::ColorIsEqual(pal[0], red), "LoadImagePalette single color");
            mr::UnloadImagePalette(pal);
            mr::UnloadImage(img);
        }

        // --- Image drawing ---
        {
            mr::Image img = mr::GenImageColor(16, 16, mr::BLANK);
            mr::ImageDrawRectangleRec(&img, {4, 4, 8, 8}, red);
            Check(mr::ColorIsEqual(mr::GetImageColor(img, 8, 8), red), "ImageDrawRectangleRec fill");
            Check(mr::GetImageColor(img, 0, 0).a == 0, "ImageDrawRectangleRec leaves outside clear");
            mr::ImageDrawCircle(&img, 8, 8, 3, mr::GREEN);
            Check(mr::ColorIsEqual(mr::GetImageColor(img, 8, 8), mr::GREEN), "ImageDrawCircle center");
            mr::ImageDrawLine(&img, 0, 0, 15, 15, mr::BLUE);
            Check(mr::ColorIsEqual(mr::GetImageColor(img, 0, 0), mr::BLUE), "ImageDrawLine endpoint");
            mr::UnloadImage(img);
        }

        // --- Image transforms ---
        {
            mr::Image img = mr::GenImageColor(4, 2, red);
            mr::ImageDrawPixel(&img, 0, 0, mr::GREEN);
            mr::ImageRotateCW(&img);
            Check(img.width == 2 && img.height == 4, "ImageRotateCW swaps dims");
            // The (0,0) green pixel moves to the top-right corner after CW rotation.
            Check(mr::ColorIsEqual(mr::GetImageColor(img, 1, 0), mr::GREEN), "ImageRotateCW moves pixel");
            mr::UnloadImage(img);
        }
        {
            mr::Image img = mr::GenImageColor(4, 4, red);
            mr::ImageResizeCanvas(&img, 8, 8, 2, 2, mr::BLANK);
            Check(img.width == 8 && img.height == 8, "ImageResizeCanvas dims");
            Check(mr::GetImageColor(img, 0, 0).a == 0, "ResizeCanvas fill outside");
            Check(mr::ColorIsEqual(mr::GetImageColor(img, 3, 3), red), "ResizeCanvas keeps content");
            mr::UnloadImage(img);
        }
        {
            mr::Image img = mr::GenImageColor(3, 3, red);
            mr::ImageToPOT(&img, mr::BLANK);
            Check(img.width == 4 && img.height == 4, "ImageToPOT rounds up");
            mr::UnloadImage(img);
        }

        // --- Alpha ops ---
        {
            mr::Image img = mr::GenImageColor(4, 4, mr::BLANK); // fully transparent
            mr::ImageDrawRectangleRec(&img, {1, 1, 2, 2}, red);
            mr::ImageAlphaCrop(&img, 0.5f);
            Check(img.width == 2 && img.height == 2, "ImageAlphaCrop tight");
            mr::UnloadImage(img);
        }

        // --- Extended generation ---
        {
            mr::Image p = mr::GenImagePerlinNoise(16, 16, 0, 0, 4.0f);
            Check(mr::IsImageValid(p), "GenImagePerlinNoise valid");
            mr::UnloadImage(p);
            mr::Image c = mr::GenImageCellular(16, 16, 4);
            Check(mr::IsImageValid(c), "GenImageCellular valid");
            mr::UnloadImage(c);
            mr::Image g = mr::GenImageGradientLinear(16, 16, 0, mr::BLACK, mr::WHITE);
            Check(mr::IsImageValid(g), "GenImageGradientLinear valid");
            mr::UnloadImage(g);
        }

        // --- Alpha border ---
        {
            mr::Image img = mr::GenImageColor(10, 10, mr::BLANK);
            mr::ImageDrawRectangleRec(&img, {3, 4, 2, 2}, red);
            mr::Rectangle bd = mr::GetImageAlphaBorder(img, 0.5f);
            Check((int)bd.x == 3 && (int)bd.y == 4 && (int)bd.width == 2 && (int)bd.height == 2, "GetImageAlphaBorder");
            mr::UnloadImage(img);
        }

        // --- ImageDither: quantizing to low bit depth reduces distinct colors ---
        {
            mr::Image grad = mr::GenImageGradientH(64, 4, mr::BLACK, mr::WHITE);
            mr::ImageFormat(&grad, mr::PixelFormat::Uncompressed_R8G8B8A8);
            auto distinctReds = [](const mr::Image& im) {
                bool seen[256] = {false}; int n = 0;
                for (int x = 0; x < im.width; ++x) { int r = mr::GetImageColor(im, x, 0).r; if (!seen[r]) { seen[r] = true; ++n; } }
                return n;
            };
            const int before = distinctReds(grad);
            mr::ImageDither(&grad, 2, 2, 2, 0);   // 2 bits/channel -> few levels
            const int after = distinctReds(grad);
            Check(after < before, "ImageDither reduces the number of distinct levels");
            Check(after <= 8, "ImageDither quantizes to the target bit depth");
            mr::UnloadImage(grad);
        }

        std::printf("textures_util_checks: all checks passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "textures_util_checks FAILED: %s\n", e.what());
        return 1;
    }
}
