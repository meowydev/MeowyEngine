// png_bbox - tiny standalone PNG inspector for backend ImGui render proofs.
//
// Usage: png_bbox <image.png> [rmin gmax bmax]
// Prints the image size and the bounding box of pixels matching the "reddish"
// predicate (default r>180, g<80, b<80), plus a couple of sampled pixels. Used
// to verify the pinned solid-red ImGui window actually rendered.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <png> [rmin gmax bmax]\n", argv[0]); return 2; }
    int rmin = argc > 2 ? atoi(argv[2]) : 180;
    int gmax = argc > 3 ? atoi(argv[3]) : 80;
    int bmax = argc > 4 ? atoi(argv[4]) : 80;
    int w, h, n;
    unsigned char* px = stbi_load(argv[1], &w, &h, &n, 4);
    if (!px) { fprintf(stderr, "failed to load %s\n", argv[1]); return 1; }
    printf("size %dx%d channels %d\n", w, h, n);
    long minx = w, miny = h, maxx = -1, maxy = -1, count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned char* p = px + (y * (long)w + x) * 4;
            if (p[0] > rmin && p[1] < gmax && p[2] < bmax) {
                ++count;
                if (x < minx) minx = x; if (x > maxx) maxx = x;
                if (y < miny) miny = y; if (y > maxy) maxy = y;
            }
        }
    }
    if (count) printf("red count %ld bbox x[%ld..%ld] y[%ld..%ld]\n", count, minx, maxx, miny, maxy);
    else       printf("red count 0 (no matching pixels)\n");
    stbi_image_free(px);
    return count ? 0 : 3;
}
