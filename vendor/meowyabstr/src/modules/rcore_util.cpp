// meowyrender - src/modules/rcore_util.cpp
// rcore CPU utilities for raylib 6.0 parity: memory helpers, Base64 encode/
// decode, DEFLATE-family compression (via the vendored stb zlib), CRC32/MD5/
// SHA1/SHA256 hashing, unique random sequences, and screen/world/camera math.
//
// These are backend-independent and deterministic, so they are covered by unit
// tests without a graphics context.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>

// stb provides zlib decode (RFC1950) here; the matching compressor lives in
// stb_image_write. Declare the entry points we use.
extern "C" {
unsigned char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);
unsigned char* stbi_zlib_decode_malloc_guesssize_headerflag(const char* buffer, int len,
                                                            int initial_size, int* outlen, int parse_header);
}
// stb_image_write's zlib compressor, re-exported from stb_impl.cpp.
extern "C" unsigned char* MeowyZlibCompress(unsigned char* data, int data_len, int* out_len, int quality);

namespace meowyrender {

// ===========================================================================
// Memory helpers (raylib: paired with MemFree)
// ===========================================================================
void* MemAlloc(unsigned int size) { return std::calloc(1, size ? size : 1); }
void* MemRealloc(void* ptr, unsigned int size) { return std::realloc(ptr, size ? size : 1); }
void MemFree(void* ptr) { std::free(ptr); }

// ===========================================================================
// Base64 (raylib-compatible: standard alphabet, '=' padding, NUL terminator)
// ===========================================================================
namespace {
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int B64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1; // '=' or invalid
}
} // namespace

char* EncodeDataBase64(const unsigned char* data, int dataSize, int* outputSize) {
    if (!data || dataSize < 0) { if (outputSize) *outputSize = 0; return nullptr; }
    const int encodedLen = 4 * ((dataSize + 2) / 3);
    char* out = static_cast<char*>(std::malloc(static_cast<std::size_t>(encodedLen) + 1));
    int o = 0;
    for (int i = 0; i < dataSize; i += 3) {
        const std::uint32_t a = data[i];
        const std::uint32_t b = (i + 1 < dataSize) ? data[i + 1] : 0;
        const std::uint32_t c = (i + 2 < dataSize) ? data[i + 2] : 0;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        out[o++] = kB64[(triple >> 18) & 0x3F];
        out[o++] = kB64[(triple >> 12) & 0x3F];
        out[o++] = (i + 1 < dataSize) ? kB64[(triple >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < dataSize) ? kB64[triple & 0x3F] : '=';
    }
    out[o] = '\0';
    if (outputSize) *outputSize = encodedLen + 1; // includes terminator (raylib)
    return out;
}

unsigned char* DecodeDataBase64(const char* text, int* outputSize) {
    if (!text) { if (outputSize) *outputSize = 0; return nullptr; }
    const int len = static_cast<int>(std::strlen(text));
    std::vector<int> vals;
    vals.reserve(len);
    for (int i = 0; i < len; ++i) {
        if (text[i] == '=' || text[i] == '\n' || text[i] == '\r') continue;
        const int v = B64Value(text[i]);
        if (v >= 0) vals.push_back(v);
    }
    const int outLen = static_cast<int>(vals.size()) * 3 / 4;
    unsigned char* out = static_cast<unsigned char*>(std::malloc(outLen > 0 ? outLen : 1));
    int o = 0;
    for (std::size_t i = 0; i + 1 < vals.size(); i += 4) {
        const std::uint32_t t = (vals[i] << 18) | (vals[i + 1] << 12) |
                                ((i + 2 < vals.size() ? vals[i + 2] : 0) << 6) |
                                (i + 3 < vals.size() ? vals[i + 3] : 0);
        if (o < outLen) out[o++] = (t >> 16) & 0xFF;
        if (o < outLen && i + 2 < vals.size()) out[o++] = (t >> 8) & 0xFF;
        if (o < outLen && i + 3 < vals.size()) out[o++] = t & 0xFF;
    }
    if (outputSize) *outputSize = o;
    return out;
}

// ===========================================================================
// Compression. raylib's CompressData/DecompressData use RAW DEFLATE (no zlib
// wrapper). stb produces a zlib stream (2-byte header + 4-byte adler32
// trailer); we strip those on compress and decode with parse_header=0 so the
// on-wire bytes are raw DEFLATE, matching raylib's format.
// ===========================================================================
unsigned char* CompressData(const unsigned char* data, int dataSize, int* compDataSize) {
    if (!data || dataSize <= 0) { if (compDataSize) *compDataSize = 0; return nullptr; }
    int zlibLen = 0;
    unsigned char* zlib = MeowyZlibCompress(const_cast<unsigned char*>(data), dataSize, &zlibLen, 8);
    if (!zlib || zlibLen < 6) { std::free(zlib); if (compDataSize) *compDataSize = 0; return nullptr; }
    // Strip the 2-byte zlib header and the 4-byte adler32 trailer -> raw DEFLATE.
    const int rawLen = zlibLen - 6;
    unsigned char* out = static_cast<unsigned char*>(std::malloc(rawLen > 0 ? rawLen : 1));
    std::memcpy(out, zlib + 2, static_cast<std::size_t>(rawLen));
    std::free(zlib);
    if (compDataSize) *compDataSize = rawLen;
    return out;
}

unsigned char* DecompressData(const unsigned char* compData, int compDataSize, int* dataSize) {
    if (!compData || compDataSize <= 0) { if (dataSize) *dataSize = 0; return nullptr; }
    int outLen = 0;
    // parse_header=0: input is raw DEFLATE (raylib format), no zlib wrapper.
    unsigned char* out = stbi_zlib_decode_malloc_guesssize_headerflag(
        reinterpret_cast<const char*>(compData), compDataSize,
        compDataSize * 4 + 64, &outLen, /*parse_header=*/0);
    if (dataSize) *dataSize = out ? outLen : 0;
    return out;
}

// ===========================================================================
// CRC32 (IEEE 802.3, matches raylib/zlib polynomial)
// ===========================================================================
unsigned int ComputeCRC32(unsigned char* data, int dataSize) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < dataSize; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ===========================================================================
// MD5 (RFC 1321). Returns a pointer to a static uint[4] (raylib semantics).
// ===========================================================================
namespace {
std::uint32_t Rotl(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
}
unsigned int* ComputeMD5(unsigned char* data, int dataSize) {
    static const std::uint32_t s[] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    static const std::uint32_t K[] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static std::uint32_t h[4];
    h[0]=0x67452301;h[1]=0xefcdab89;h[2]=0x98badcfe;h[3]=0x10325476;
    std::vector<unsigned char> msg(data, data + dataSize);
    const std::uint64_t bitLen = static_cast<std::uint64_t>(dataSize) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 0; i < 8; ++i) msg.push_back((bitLen >> (8*i)) & 0xFF);
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t M[16];
        for (int i = 0; i < 16; ++i)
            M[i] = msg[off+i*4] | (msg[off+i*4+1]<<8) | (msg[off+i*4+2]<<16) | (std::uint32_t(msg[off+i*4+3])<<24);
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f; int g;
            if (i<16){f=(b&c)|(~b&d);g=i;}
            else if (i<32){f=(d&b)|(~d&c);g=(5*i+1)%16;}
            else if (i<48){f=b^c^d;g=(3*i+5)%16;}
            else {f=c^(b|~d);g=(7*i)%16;}
            f=f+a+K[i]+M[g];a=d;d=c;c=b;b=b+Rotl(f,s[i]);
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;
    }
    return h;
}

// ===========================================================================
// SHA1 (RFC 3174). Returns a pointer to a static uint[5] (raylib semantics).
// ===========================================================================
unsigned int* ComputeSHA1(unsigned char* data, int dataSize) {
    static std::uint32_t h[5];
    h[0]=0x67452301;h[1]=0xEFCDAB89;h[2]=0x98BADCFE;h[3]=0x10325476;h[4]=0xC3D2E1F0;
    std::vector<unsigned char> msg(data, data + dataSize);
    const std::uint64_t bitLen = static_cast<std::uint64_t>(dataSize) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back((bitLen >> (8*i)) & 0xFF);
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(msg[off+i*4])<<24)|(msg[off+i*4+1]<<16)|(msg[off+i*4+2]<<8)|msg[off+i*4+3];
        for (int i = 16; i < 80; ++i) w[i] = Rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f,k;
            if (i<20){f=(b&c)|(~b&d);k=0x5A827999;}
            else if (i<40){f=b^c^d;k=0x6ED9EBA1;}
            else if (i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
            else {f=b^c^d;k=0xCA62C1D6;}
            std::uint32_t t=Rotl(a,5)+f+e+k+w[i];e=d;d=c;c=Rotl(b,30);b=a;a=t;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
    }
    return h;
}

// ===========================================================================
// SHA256 (FIPS 180-4). MeowyRender extension; returns a static uint[8].
// ===========================================================================
unsigned int* ComputeSHA256(unsigned char* data, int dataSize) {
    static const std::uint32_t K[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    static std::uint32_t h[8];
    h[0]=0x6a09e667;h[1]=0xbb67ae85;h[2]=0x3c6ef372;h[3]=0xa54ff53a;
    h[4]=0x510e527f;h[5]=0x9b05688c;h[6]=0x1f83d9ab;h[7]=0x5be0cd19;
    auto rotr=[](std::uint32_t x,int n){return (x>>n)|(x<<(32-n));};
    std::vector<unsigned char> msg(data, data + dataSize);
    const std::uint64_t bitLen = static_cast<std::uint64_t>(dataSize) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back((bitLen >> (8*i)) & 0xFF);
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i]=(std::uint32_t(msg[off+i*4])<<24)|(msg[off+i*4+1]<<16)|(msg[off+i*4+2]<<8)|msg[off+i*4+3];
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            std::uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            std::uint32_t ch=(e&f)^(~e&g);
            std::uint32_t t1=hh+S1+ch+K[i]+w[i];
            std::uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            std::uint32_t maj=(a&b)^(a&c)^(b&c);
            std::uint32_t t2=S0+maj;
            hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    return h;
}

// ===========================================================================
// Random sequence (unique values in [min,max])
// ===========================================================================
int* LoadRandomSequence(unsigned int count, int min, int max) {
    if (max < min) std::swap(min, max);
    const unsigned int range = static_cast<unsigned int>(max - min + 1);
    if (count == 0 || count > range) return nullptr; // raylib: no repeats possible
    std::vector<int> pool(range);
    for (unsigned int i = 0; i < range; ++i) pool[i] = min + static_cast<int>(i);
    // Partial Fisher-Yates using the same global RNG SetRandomSeed controls.
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned int j = i + static_cast<unsigned int>(std::rand()) % (range - i);
        std::swap(pool[i], pool[j]);
    }
    int* out = static_cast<int*>(std::malloc(count * sizeof(int)));
    for (unsigned int i = 0; i < count; ++i) out[i] = pool[i];
    return out;
}
void UnloadRandomSequence(int* sequence) { std::free(sequence); }

// ===========================================================================
// UpdateCameraPro: raylib-parity free-camera controller driven by explicit
// movement/rotation/zoom deltas (no direct input polling).
// ===========================================================================
void UpdateCameraPro(Camera3D* camera, Vector3 movement, Vector3 rotation, float zoom) {
    if (!camera) return;
    // rotation = { yaw (deg), pitch (deg), roll (deg) }.
    const bool lockView = true;      // raylib CameraPro keeps target-follow behavior
    const bool rotateAroundTarget = false;
    const bool rotateUp = false;

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));

    // --- Rotation (yaw around up, pitch around right) ---
    auto rotateAxis = [](Vector3 v, Vector3 axis, float angle) {
        return Vector3Transform(v, MatrixRotate(axis, angle));
    };
    const float yaw = -rotation.x * DEG2RAD;
    const float pitch = -rotation.y * DEG2RAD;

    forward = rotateAxis(forward, camera->up, yaw);
    // Clamp pitch so forward doesn't flip past the up vector.
    float maxAngleUp = std::acos(Vector3DotProduct(camera->up, forward)) - 0.001f;
    float maxAngleDown = -(3.14159265f - std::acos(Vector3DotProduct(camera->up, forward))) + 0.001f;
    float p = pitch;
    if (p > maxAngleUp) p = maxAngleUp;
    if (p < maxAngleDown) p = maxAngleDown;
    forward = rotateAxis(forward, right, p);
    right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));

    // --- Movement (x forward, y right, z up) ---
    Vector3 up = camera->up;
    Vector3 move{};
    move = Vector3Add(move, Vector3Scale(forward, movement.x));
    move = Vector3Add(move, Vector3Scale(right, movement.y));
    move = Vector3Add(move, Vector3Scale(up, movement.z));
    camera->position = Vector3Add(camera->position, move);

    if (lockView && !rotateAroundTarget) {
        camera->target = Vector3Add(camera->position, forward);
    } else {
        camera->position = Vector3Subtract(camera->target, Vector3Scale(forward, Vector3Length(
            Vector3Subtract(camera->target, camera->position))));
    }

    // --- Zoom: move along the forward axis toward/away from target ---
    if (zoom != 0.0f) {
        camera->position = Vector3Add(camera->position, Vector3Scale(forward, zoom));
        if (lockView) camera->target = Vector3Add(camera->position, forward);
    }
    (void)rotateUp;
}

// ===========================================================================
// Screen/world space + camera matrices
// ===========================================================================
Matrix GetCameraMatrix2D(Camera2D camera) {
    // raylib: MatrixTranslate(-target) * MatrixRotate(rotation) *
    //         MatrixScale(zoom) * MatrixTranslate(offset)
    Matrix origin = MatrixTranslate(-camera.target.x, -camera.target.y, 0);
    const float rad = camera.rotation * DEG2RAD;
    Matrix rot = MatrixIdentity();
    rot.m0 = std::cos(rad); rot.m4 = -std::sin(rad);
    rot.m1 = std::sin(rad); rot.m5 =  std::cos(rad);
    Matrix scale = MatrixScale(camera.zoom, camera.zoom, 1);
    Matrix off = MatrixTranslate(camera.offset.x, camera.offset.y, 0);
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(origin, rot), scale), off);
}

Vector2 GetWorldToScreen2D(Vector2 position, Camera2D camera) {
    Matrix m = GetCameraMatrix2D(camera);
    Vector3 t = Vector3Transform({position.x, position.y, 0}, m);
    return {t.x, t.y};
}

Vector2 GetScreenToWorld2D(Vector2 position, Camera2D camera) {
    Matrix inv = MatrixInvert(GetCameraMatrix2D(camera));
    Vector3 t = Vector3Transform({position.x, position.y, 0}, inv);
    return {t.x, t.y};
}

Ray GetScreenToWorldRayEx(Vector2 position, Camera3D camera, int width, int height) {
    Ray ray{};
    const float x = (2.0f * position.x) / width - 1.0f;
    const float y = 1.0f - (2.0f * position.y) / height;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    if (camera.projection == CameraProjection::Perspective) {
        const float aspect = static_cast<float>(width) / height;
        const float tanF = std::tan(camera.fovy * 0.5f * DEG2RAD);
        Vector3 dir = Vector3Normalize(Vector3Add(forward,
            Vector3Add(Vector3Scale(right, x * tanF * aspect), Vector3Scale(up, y * tanF))));
        ray.position = camera.position;
        ray.direction = dir;
    } else {
        // Orthographic: parallel rays offset across the view plane.
        const float aspect = static_cast<float>(width) / height;
        const float top = camera.fovy / 2.0f;
        ray.position = Vector3Add(camera.position,
            Vector3Add(Vector3Scale(right, x * top * aspect), Vector3Scale(up, y * top)));
        ray.direction = forward;
    }
    return ray;
}

Ray GetScreenToWorldRay(Vector2 position, Camera3D camera) {
    auto& s = detail::State();
    return GetScreenToWorldRayEx(position, camera, s.screenWidth, s.screenHeight);
}

Vector2 GetWorldToScreenEx(Vector3 position, Camera3D camera, int width, int height) {
    const float aspect = static_cast<float>(width) / height;
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);
    Vector3 viewPos = Vector3Transform(position, view);
    const float w = -viewPos.z;
    if (w <= 0.0f) return {-1, -1};
    Matrix proj = (camera.projection == CameraProjection::Perspective)
        ? MatrixPerspective(camera.fovy * DEG2RAD, aspect, 0.01, 1000.0)
        : MatrixOrtho(-camera.fovy/2*aspect, camera.fovy/2*aspect, -camera.fovy/2, camera.fovy/2, 0.01, 1000.0);
    Vector3 clip = Vector3Transform(position, MatrixMultiply(view, proj));
    const float ndcX = clip.x / w, ndcY = clip.y / w;
    return {(ndcX + 1.0f) * 0.5f * width, (1.0f - (ndcY + 1.0f) * 0.5f) * height};
}

} // namespace meowyrender
