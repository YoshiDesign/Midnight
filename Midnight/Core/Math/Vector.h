#pragma once

namespace aveng{
    
    struct Vec2 {
        float x = 0.f;
        float y = 0.f;

        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}

        Vec2 operator-(const Vec2& rhs) const { return { x - rhs.x, y - rhs.y }; }
        Vec2 operator+(const Vec2& rhs) const { return { x + rhs.x, y + rhs.y }; }
        Vec2 operator*(float s) const { return { x * s, y * s }; }

        float dot(const Vec2& rhs) const { return x * rhs.x + y * rhs.y; }
        float cross(const Vec2& rhs) const { return x * rhs.y - y * rhs.x; }

        float len2() const { return x * x + y * y; }
    };

	struct Vec3 {
		float x = 0.0;
		float y = 0.0;
		float z = 0.0;
	
		Vec3() = default;
		Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		
	};
	
}
