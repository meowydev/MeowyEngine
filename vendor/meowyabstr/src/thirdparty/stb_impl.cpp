// meowyrender - src/thirdparty/stb_impl.cpp  (internal)
//
// Single translation unit that compiles the implementations of the stb
// single-header libraries used by meowyrender. These are public-domain
// libraries by Sean Barrett (https://github.com/nothings/stb) and are vendored
// under third_party/stb. Only their implementation macros live here so the
// rest of the codebase includes the headers declaration-only.
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// Vorbis is declaration-only in audio.cpp; compile its implementation here.
#include "stb_vorbis.c"

// Expose stb_image_write's zlib (RFC1950) compressor for rcore CompressData.
// stbi_zlib_compress is declared by stb_image_write.h; the returned buffer is
// malloc'd and freed by callers via MemFree (std::free).
extern "C" unsigned char* MeowyZlibCompress(unsigned char* data, int data_len,
                                            int* out_len, int quality) {
    return stbi_zlib_compress(data, data_len, out_len, quality);
}
