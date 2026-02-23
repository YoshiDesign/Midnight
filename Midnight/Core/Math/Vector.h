#pragma once
#include <cmath>
namespace aveng{
    
    struct Vec2 {
        float x = 0.f;
        float y = 0.f; // y == world z

        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}

        Vec2 operator-(const Vec2& rhs) const { return { x - rhs.x, y - rhs.y }; }
        Vec2 operator+(const Vec2& rhs) const { return { x + rhs.x, y + rhs.y }; }
        Vec2 operator*(float s) const { return { x * s, y * s }; }

        float dot(const Vec2& rhs) const { return x * rhs.x + y * rhs.y; }
        float cross(const Vec2& rhs) const { return x * rhs.y - y * rhs.x; }

        float len2() const { return x * x + y * y; }
        float len() const { return std::sqrt(len2()); }
    };

    struct Vec3 {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

        Vec3 operator-(const Vec3& rhs) const { return { x - rhs.x, y - rhs.y, z - rhs.z }; }
        Vec3 operator+(const Vec3& rhs) const { return { x + rhs.x, y + rhs.y, z + rhs.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

        float len2() const { return x * x + y * y + z * z; }
        float len() const { return std::sqrt(len2()); }

        static Vec3 cross(const Vec3& a, const Vec3& b) {
            return Vec3{
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        }

        float dot(const Vec3& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }

        Vec3 normalized(float eps = 1e-20f) const {
            float L = len();
            if (L <= eps) return Vec3{ 0.f, 1.f, 0.f };
            return (*this) * (1.f / L);
        }
    };

    // ---- Core helpers ----
    inline float cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
	
}
