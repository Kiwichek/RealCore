#pragma once
#include <cmath>
#include <cstring>

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    Vec3 operator-(const Vec3& v) const { return { x - v.x, y - v.y, z - v.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }
    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }

    Vec3 cross(const Vec3& v) const {
        return { y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x };
    }
    float dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const { float l = length(); return l > 0 ? *this / l : *this; }
};

struct Mat4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    Mat4 operator*(const Mat4& r) const {
        Mat4 res;
        for (int col = 0; col < 4; col++)
            for (int row = 0; row < 4; row++)
                res.m[col * 4 + row] =
                    m[0 * 4 + row] * r.m[col * 4 + 0] +
                    m[1 * 4 + row] * r.m[col * 4 + 1] +
                    m[2 * 4 + row] * r.m[col * 4 + 2] +
                    m[3 * 4 + row] * r.m[col * 4 + 3];
        return res;
    }

    Vec3 transformPoint(const Vec3& p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
        };
    }

    static Mat4 perspective(float fovY, float aspect, float znear, float zfar) {
        Mat4 r;
        float f = 1.0f / std::tan(fovY * 0.5f);
        for (auto& v : r.m) v = 0;
        r.m[0]  = f / aspect;
        r.m[5]  = f;
        r.m[10] = zfar / (znear - zfar);
        r.m[11] = -1;
        r.m[14] = znear * zfar / (znear - zfar);
        return r;
    }

    static Mat4 orthographic(float left, float right, float bottom, float top, float znear, float zfar) {
        Mat4 r;
        for (auto& v : r.m) v = 0;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = 1.0f / (znear - zfar);
        r.m[12] = (left + right) / (left - right);
        r.m[13] = (top + bottom) / (bottom - top);
        r.m[14] = znear / (znear - zfar);
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);

        Mat4 r;
        r.m[0]  = s.x; r.m[4]  = s.y; r.m[8]  = s.z; r.m[12] = -s.dot(eye);
        r.m[1]  = u.x; r.m[5]  = u.y; r.m[9]  = u.z; r.m[13] = -u.dot(eye);
        r.m[2]  = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = f.dot(eye);
        r.m[3]  = 0;   r.m[7]  = 0;   r.m[11] = 0;   r.m[15]  = 1;
        return r;
    }

    static Mat4 rotateY(float angle) {
        Mat4 r;
        float c = std::cos(angle), s = std::sin(angle);
        r.m[0] = c; r.m[2] = s;
        r.m[8] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateX(float angle) {
        Mat4 r;
        float c = std::cos(angle), s = std::sin(angle);
        r.m[5] = c; r.m[6] = -s;
        r.m[9] = s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateZ(float angle) {
        Mat4 r;
        float c = std::cos(angle), s = std::sin(angle);
        r.m[0] = c; r.m[1] = s;
        r.m[4] = -s; r.m[5] = c;
        return r;
    }

    static Mat4 scale(const Vec3& v) {
        Mat4 r;
        r.m[0] = v.x;
        r.m[5] = v.y;
        r.m[10] = v.z;
        return r;
    }

    static Mat4 translate(const Vec3& v) {
        Mat4 r;
        r.m[12] = v.x;
        r.m[13] = v.y;
        r.m[14] = v.z;
        return r;
    }
};
