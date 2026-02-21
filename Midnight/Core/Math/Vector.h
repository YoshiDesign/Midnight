#pragma once

namespace aveng{
    
	struct Vec2 {
		float x = 0.0;
		float y = 0.0;
	
		Vec2() = default;
		Vec2(float x_, float y_) : x(x_), y(y_) {}
	
		Vec2 operator-(const Vec2& rhs) const { return Vec2{x - rhs.x, y - rhs.y}; }
		float len2() const { return x * x + y * y; }
		// This equals the signed area scale used by many barycentric/tri tests.
		float cross2(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
	};

	struct Vec3 {
		float x = 0.0;
		float y = 0.0;
		float z = 0.0;
	
		Vec3() = default;
		Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		
	};
	
}