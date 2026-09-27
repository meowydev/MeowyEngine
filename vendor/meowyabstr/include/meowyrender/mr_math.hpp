// meowyrender - mr_math.hpp
// Math primitives mirroring raylib's raymath, in the meowyrender namespace.
#pragma once

#include <cmath>
#include <cstdint>

namespace meowyrender {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
inline constexpr float PI = 3.14159265358979323846f;
inline constexpr float DEG2RAD = PI / 180.0f;
inline constexpr float RAD2DEG = 180.0f / PI;

// ---------------------------------------------------------------------------
// Vector types
// ---------------------------------------------------------------------------
struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vector4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

using Quaternion = Vector4;

// Column-major 4x4 matrix (OpenGL style), same field layout as raylib.
struct Matrix {
    float m0 = 1, m4 = 0, m8  = 0, m12 = 0;
    float m1 = 0, m5 = 1, m9  = 0, m13 = 0;
    float m2 = 0, m6 = 0, m10 = 1, m14 = 0;
    float m3 = 0, m7 = 0, m11 = 0, m15 = 1;
};

// ---------------------------------------------------------------------------
// Scalar helpers
// ---------------------------------------------------------------------------
[[nodiscard]] inline float Clamp(float value, float min, float max) {
    const float r = value < min ? min : value;
    return r > max ? max : r;
}

[[nodiscard]] inline float Lerp(float start, float end, float amount) {
    return start + amount * (end - start);
}

[[nodiscard]] inline float Normalize(float value, float start, float end) {
    return (value - start) / (end - start);
}

[[nodiscard]] inline float Remap(float value, float inStart, float inEnd,
                                  float outStart, float outEnd) {
    return (value - inStart) / (inEnd - inStart) * (outEnd - outStart) + outStart;
}

[[nodiscard]] inline float Wrap(float value, float min, float max) {
    return value - (max - min) * std::floor((value - min) / (max - min));
}

// ---------------------------------------------------------------------------
// Vector2
// ---------------------------------------------------------------------------
[[nodiscard]] inline Vector2 Vector2Zero() { return {0.0f, 0.0f}; }
[[nodiscard]] inline Vector2 Vector2One() { return {1.0f, 1.0f}; }

[[nodiscard]] inline Vector2 Vector2Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] inline Vector2 Vector2Subtract(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] inline Vector2 Vector2Scale(Vector2 v, float s) { return {v.x * s, v.y * s}; }
[[nodiscard]] inline Vector2 Vector2Multiply(Vector2 a, Vector2 b) { return {a.x * b.x, a.y * b.y}; }
[[nodiscard]] inline Vector2 Vector2Negate(Vector2 v) { return {-v.x, -v.y}; }

[[nodiscard]] inline float Vector2DotProduct(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
[[nodiscard]] inline float Vector2Length(Vector2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
[[nodiscard]] inline float Vector2LengthSqr(Vector2 v) { return v.x * v.x + v.y * v.y; }

[[nodiscard]] inline float Vector2Distance(Vector2 a, Vector2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

[[nodiscard]] inline float Vector2Angle(Vector2 a, Vector2 b) {
    return std::atan2(b.y - a.y, b.x - a.x);
}

[[nodiscard]] inline Vector2 Vector2Normalize(Vector2 v) {
    const float len = Vector2Length(v);
    if (len > 0.0f) return {v.x / len, v.y / len};
    return {0.0f, 0.0f};
}

[[nodiscard]] inline Vector2 Vector2Lerp(Vector2 a, Vector2 b, float amount) {
    return {a.x + amount * (b.x - a.x), a.y + amount * (b.y - a.y)};
}

[[nodiscard]] inline Vector2 Vector2Rotate(Vector2 v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// ---------------------------------------------------------------------------
// Vector3
// ---------------------------------------------------------------------------
[[nodiscard]] inline Vector3 Vector3Zero() { return {0.0f, 0.0f, 0.0f}; }
[[nodiscard]] inline Vector3 Vector3One() { return {1.0f, 1.0f, 1.0f}; }

[[nodiscard]] inline Vector3 Vector3Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] inline Vector3 Vector3Subtract(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] inline Vector3 Vector3Scale(Vector3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
[[nodiscard]] inline Vector3 Vector3Multiply(Vector3 a, Vector3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
[[nodiscard]] inline Vector3 Vector3Negate(Vector3 v) { return {-v.x, -v.y, -v.z}; }

[[nodiscard]] inline Vector3 Vector3CrossProduct(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline float Vector3DotProduct(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline float Vector3Length(Vector3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

[[nodiscard]] inline float Vector3Distance(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

[[nodiscard]] inline Vector3 Vector3Normalize(Vector3 v) {
    const float len = Vector3Length(v);
    if (len > 0.0f) return {v.x / len, v.y / len, v.z / len};
    return {0.0f, 0.0f, 0.0f};
}

[[nodiscard]] inline Vector3 Vector3Lerp(Vector3 a, Vector3 b, float amount) {
    return {a.x + amount * (b.x - a.x),
            a.y + amount * (b.y - a.y),
            a.z + amount * (b.z - a.z)};
}

// ---------------------------------------------------------------------------
// Matrix
// ---------------------------------------------------------------------------
[[nodiscard]] inline Matrix MatrixIdentity() { return Matrix{}; }

[[nodiscard]] inline Matrix MatrixMultiply(const Matrix& a, const Matrix& b) {
    Matrix r;
    r.m0  = a.m0 * b.m0  + a.m1 * b.m4  + a.m2 * b.m8   + a.m3 * b.m12;
    r.m1  = a.m0 * b.m1  + a.m1 * b.m5  + a.m2 * b.m9   + a.m3 * b.m13;
    r.m2  = a.m0 * b.m2  + a.m1 * b.m6  + a.m2 * b.m10  + a.m3 * b.m14;
    r.m3  = a.m0 * b.m3  + a.m1 * b.m7  + a.m2 * b.m11  + a.m3 * b.m15;
    r.m4  = a.m4 * b.m0  + a.m5 * b.m4  + a.m6 * b.m8   + a.m7 * b.m12;
    r.m5  = a.m4 * b.m1  + a.m5 * b.m5  + a.m6 * b.m9   + a.m7 * b.m13;
    r.m6  = a.m4 * b.m2  + a.m5 * b.m6  + a.m6 * b.m10  + a.m7 * b.m14;
    r.m7  = a.m4 * b.m3  + a.m5 * b.m7  + a.m6 * b.m11  + a.m7 * b.m15;
    r.m8  = a.m8 * b.m0  + a.m9 * b.m4  + a.m10 * b.m8  + a.m11 * b.m12;
    r.m9  = a.m8 * b.m1  + a.m9 * b.m5  + a.m10 * b.m9  + a.m11 * b.m13;
    r.m10 = a.m8 * b.m2  + a.m9 * b.m6  + a.m10 * b.m10 + a.m11 * b.m14;
    r.m11 = a.m8 * b.m3  + a.m9 * b.m7  + a.m10 * b.m11 + a.m11 * b.m15;
    r.m12 = a.m12 * b.m0 + a.m13 * b.m4 + a.m14 * b.m8  + a.m15 * b.m12;
    r.m13 = a.m12 * b.m1 + a.m13 * b.m5 + a.m14 * b.m9  + a.m15 * b.m13;
    r.m14 = a.m12 * b.m2 + a.m13 * b.m6 + a.m14 * b.m10 + a.m15 * b.m14;
    r.m15 = a.m12 * b.m3 + a.m13 * b.m7 + a.m14 * b.m11 + a.m15 * b.m15;
    return r;
}

[[nodiscard]] inline Matrix MatrixTranslate(float x, float y, float z) {
    Matrix r;
    r.m12 = x; r.m13 = y; r.m14 = z;
    return r;
}

[[nodiscard]] inline Matrix MatrixScale(float x, float y, float z) {
    Matrix r;
    r.m0 = x; r.m5 = y; r.m10 = z;
    return r;
}

// Orthographic projection (column-major), matches raylib's rlMatrixOrtho semantics.
[[nodiscard]] inline Matrix MatrixOrtho(double left, double right,
                                        double bottom, double top,
                                        double nearP, double farP) {
    Matrix r{};
    const float rl = static_cast<float>(right - left);
    const float tb = static_cast<float>(top - bottom);
    const float fn = static_cast<float>(farP - nearP);
    r.m0  = 2.0f / rl;
    r.m5  = 2.0f / tb;
    r.m10 = -2.0f / fn;
    r.m12 = -static_cast<float>(right + left) / rl;
    r.m13 = -static_cast<float>(top + bottom) / tb;
    r.m14 = -static_cast<float>(farP + nearP) / fn;
    r.m15 = 1.0f;
    return r;
}

[[nodiscard]] inline Matrix MatrixTranspose(const Matrix& m) {
    Matrix r;
    r.m0 = m.m0;  r.m1 = m.m4;  r.m2 = m.m8;   r.m3 = m.m12;
    r.m4 = m.m1;  r.m5 = m.m5;  r.m6 = m.m9;   r.m7 = m.m13;
    r.m8 = m.m2;  r.m9 = m.m6;  r.m10 = m.m10; r.m11 = m.m14;
    r.m12 = m.m3; r.m13 = m.m7; r.m14 = m.m11; r.m15 = m.m15;
    return r;
}

// Rotation matrix from axis + angle (radians).
[[nodiscard]] inline Matrix MatrixRotate(Vector3 axis, float angle) {
    Matrix r;
    float x = axis.x, y = axis.y, z = axis.z;
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len != 0.0f) { x /= len; y /= len; z /= len; }
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    const float t = 1.0f - c;
    r.m0 = x * x * t + c;      r.m4 = x * y * t - z * s;  r.m8 = x * z * t + y * s;
    r.m1 = y * x * t + z * s;  r.m5 = y * y * t + c;      r.m9 = y * z * t - x * s;
    r.m2 = z * x * t - y * s;  r.m6 = z * y * t + x * s;  r.m10 = z * z * t + c;
    return r;
}

[[nodiscard]] inline Matrix MatrixRotateX(float angle) { return MatrixRotate({1, 0, 0}, angle); }
[[nodiscard]] inline Matrix MatrixRotateY(float angle) { return MatrixRotate({0, 1, 0}, angle); }
[[nodiscard]] inline Matrix MatrixRotateZ(float angle) { return MatrixRotate({0, 0, 1}, angle); }

// View matrix looking from eye toward target with the given up vector.
[[nodiscard]] inline Matrix MatrixLookAt(Vector3 eye, Vector3 target, Vector3 up) {
    Vector3 f = Vector3Normalize(Vector3Subtract(target, eye)); // forward
    Vector3 s = Vector3Normalize(Vector3CrossProduct(f, up));   // right
    Vector3 u = Vector3CrossProduct(s, f);                      // true up
    Matrix r;
    r.m0 = s.x;  r.m4 = s.y;  r.m8  = s.z;  r.m12 = -Vector3DotProduct(s, eye);
    r.m1 = u.x;  r.m5 = u.y;  r.m9  = u.z;  r.m13 = -Vector3DotProduct(u, eye);
    r.m2 = -f.x; r.m6 = -f.y; r.m10 = -f.z; r.m14 =  Vector3DotProduct(f, eye);
    r.m3 = 0;    r.m7 = 0;    r.m11 = 0;    r.m15 = 1;
    return r;
}

// Transform a Vector3 by a matrix (w=1, perspective divide ignored).
[[nodiscard]] inline Vector3 Vector3Transform(Vector3 v, const Matrix& m) {
    return {m.m0 * v.x + m.m4 * v.y + m.m8 * v.z + m.m12,
            m.m1 * v.x + m.m5 * v.y + m.m9 * v.z + m.m13,
            m.m2 * v.x + m.m6 * v.y + m.m10 * v.z + m.m14};
}

[[nodiscard]] inline Matrix MatrixPerspective(double fovY, double aspect,
                                              double nearP, double farP) {
    Matrix r{};
    const double top = nearP * std::tan(fovY * 0.5);
    const double right = top * aspect;
    const float rl = static_cast<float>(2.0 * right);
    const float tb = static_cast<float>(2.0 * top);
    const float fn = static_cast<float>(farP - nearP);
    r.m0  = static_cast<float>(nearP) * 2.0f / rl;
    r.m5  = static_cast<float>(nearP) * 2.0f / tb;
    r.m10 = -static_cast<float>(farP + nearP) / fn;
    r.m11 = -1.0f;
    r.m14 = -static_cast<float>(farP * nearP * 2.0) / fn;
    r.m15 = 0.0f;
    return r;
}

// Full 4x4 inverse (matches raymath MatrixInvert; column-major storage).
[[nodiscard]] inline Matrix MatrixInvert(const Matrix& mat) {
    Matrix result{};
    const float a00 = mat.m0,  a01 = mat.m1,  a02 = mat.m2,  a03 = mat.m3;
    const float a10 = mat.m4,  a11 = mat.m5,  a12 = mat.m6,  a13 = mat.m7;
    const float a20 = mat.m8,  a21 = mat.m9,  a22 = mat.m10, a23 = mat.m11;
    const float a30 = mat.m12, a31 = mat.m13, a32 = mat.m14, a33 = mat.m15;

    const float b00 = a00 * a11 - a01 * a10;
    const float b01 = a00 * a12 - a02 * a10;
    const float b02 = a00 * a13 - a03 * a10;
    const float b03 = a01 * a12 - a02 * a11;
    const float b04 = a01 * a13 - a03 * a11;
    const float b05 = a02 * a13 - a03 * a12;
    const float b06 = a20 * a31 - a21 * a30;
    const float b07 = a20 * a32 - a22 * a30;
    const float b08 = a20 * a33 - a23 * a30;
    const float b09 = a21 * a32 - a22 * a31;
    const float b10 = a21 * a33 - a23 * a31;
    const float b11 = a22 * a33 - a23 * a32;

    const float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (det == 0.0f) return MatrixIdentity();
    const float invDet = 1.0f / det;

    result.m0  = ( a11 * b11 - a12 * b10 + a13 * b09) * invDet;
    result.m1  = (-a01 * b11 + a02 * b10 - a03 * b09) * invDet;
    result.m2  = ( a31 * b05 - a32 * b04 + a33 * b03) * invDet;
    result.m3  = (-a21 * b05 + a22 * b04 - a23 * b03) * invDet;
    result.m4  = (-a10 * b11 + a12 * b08 - a13 * b07) * invDet;
    result.m5  = ( a00 * b11 - a02 * b08 + a03 * b07) * invDet;
    result.m6  = (-a30 * b05 + a32 * b02 - a33 * b01) * invDet;
    result.m7  = ( a20 * b05 - a22 * b02 + a23 * b01) * invDet;
    result.m8  = ( a10 * b10 - a11 * b08 + a13 * b06) * invDet;
    result.m9  = (-a00 * b10 + a01 * b08 - a03 * b06) * invDet;
    result.m10 = ( a30 * b04 - a31 * b02 + a33 * b00) * invDet;
    result.m11 = (-a20 * b04 + a21 * b02 - a23 * b00) * invDet;
    result.m12 = (-a10 * b09 + a11 * b07 - a12 * b06) * invDet;
    result.m13 = ( a00 * b09 - a01 * b07 + a02 * b06) * invDet;
    result.m14 = (-a30 * b03 + a31 * b01 - a32 * b00) * invDet;
    result.m15 = ( a20 * b03 - a21 * b01 + a22 * b00) * invDet;
    return result;
}

} // namespace meowyrender
