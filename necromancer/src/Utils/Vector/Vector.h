#pragma once

#include <math.h>

class Vec3
{
public:
	float x = 0.0f, y = 0.0f, z = 0.0f;

public:
	Vec3(void)
	{
		x = y = z = 0.0f;
	}

	void Zero()
	{
		x = y = z = 0.f;
	}

	Vec3(float X, float Y, float Z)
	{
		x = X; y = Y; z = Z;
	}

	Vec3(float *v)
	{
		x = v[0]; y = v[1]; z = v[2];
	}

	Vec3(const float *v)
	{
		x = v[0]; y = v[1]; z = v[2];
	}

	Vec3(const Vec3 &v)
	{
		x = v.x; y = v.y; z = v.z;
	}

	Vec3 &operator=(const Vec3 &v)
	{
		x = v.x; y = v.y; z = v.z; return *this;
	}

	float &operator[](int i)
	{
		return ((float *)this)[i];
	}

	float operator[](int i) const
	{
		return ((float *)this)[i];
	}

	Vec3 &operator+=(const Vec3 &v)
	{
		x += v.x; y += v.y; z += v.z; return *this;
	}

	Vec3 &operator-=(const Vec3 &v)
	{
		x -= v.x; y -= v.y; z -= v.z; return *this;
	}

	Vec3 &operator*=(const Vec3 &v)
	{
		x *= v.x; y *= v.y; z *= v.z; return *this;
	}

	Vec3 &operator/=(const Vec3 &v)
	{
		x /= v.x; y /= v.y; z /= v.z; return *this;
	}

	Vec3 &operator+=(float v)
	{
		x += v; y += v; z += v; return *this;
	}

	Vec3 &operator-=(float v)
	{
		x -= v; y -= v; z -= v; return *this;
	}

	Vec3 &operator*=(float v)
	{
		x *= v; y *= v; z *= v; return *this;
	}

	Vec3 &operator/=(float v)
	{
		x /= v; y /= v; z /= v; return *this;
	}

	Vec3 operator+(const Vec3 &v) const
	{
		return Vec3(x + v.x, y + v.y, z + v.z);
	}

	Vec3 operator-(const Vec3 &v) const
	{
		return Vec3(x - v.x, y - v.y, z - v.z);
	}

	Vec3 operator*(const Vec3 &v) const
	{
		return Vec3(x * v.x, y * v.y, z * v.z);
	}

	Vec3 operator/(const Vec3 &v) const
	{
		return Vec3(x / v.x, y / v.y, z / v.z);
	}

	Vec3 operator+(float v) const
	{
		return Vec3(x + v, y + v, z + v);
	}

	Vec3 operator-(float v) const
	{
		return Vec3(x - v, y - v, z - v);
	}

	Vec3 operator*(float v) const
	{
		return Vec3(x * v, y * v, z * v);
	}

	Vec3 operator/(float v) const
	{
		return Vec3(x / v, y / v, z / v);
	}

	void Set(float X = 0.0f, float Y = 0.0f, float Z = 0.0f)
	{
		x = X; y = Y; z = Z;
	}

	float Length(void) const
	{
		return sqrtf(x * x + y * y + z * z);
	}

	float LengthSqr(void) const
	{
		return (x * x + y * y + z * z);
	}

	float Normalize()
	{
		float fl_Length = Length();
		float fl_Length_normal = 1.f / (1.192092896e-07F + fl_Length);

		x = x * fl_Length_normal;
		y = y * fl_Length_normal;
		z = z * fl_Length_normal;

		return fl_Length;
	}

	float NormalizeInPlace()
	{
		return Normalize();
	}

	float Length2D(void) const
	{
		return sqrtf(x * x + y * y);
	}

	float Length2DSqr(void) const
	{
		return (x * x + y * y);
	}

	float DistTo(const Vec3 &v) const
	{
		return (*this - v).Length();
	}

	float DistToSqr(const Vec3 &v) const
	{
		return (*this - v).LengthSqr();
	}

	float Dot(const Vec3 &v) const
	{
		return (x * v.x + y * v.y + z * v.z);
	}

	Vec3 Cross(const Vec3 &v) const
	{
		return Vec3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
	}

	bool IsZero(void) const
	{
		return (x > -0.01f && x < 0.01f &&
			y > -0.01f && y < 0.01f &&
			z > -0.01f && z < 0.01f);
	}

	Vec3 Scale(float fl) {
		return Vec3(x * fl, y * fl, z * fl);
	}

	// Returns a 2D version of this vector (z = 0)
	Vec3 To2D() const {
		return Vec3(x, y, 0.0f);
	}

	// Returns a normalized 2D version of this vector
	Vec3 Normalized2D() const {
		float len = Length2D();
		if (len > 0.0f)
			return Vec3(x / len, y / len, 0.0f);
		return Vec3(0.0f, 0.0f, 0.0f);
	}

	void Init(float ix, float iy, float iz)
	{
		x = ix; y = iy; z = iz;
	}

	bool operator==(const Vec3& v) const
	{
		return (x == v.x && y == v.y && z == v.z);
	}

	bool operator!=(const Vec3& v) const
	{
		return (x != v.x || y != v.y || z != v.z);
	}

	Vec3 operator-() const
	{
		return Vec3(-x, -y, -z);
	}

	// DeltaAngle - returns the shortest angular difference between two angles
	Vec3 DeltaAngle(const Vec3& v) const
	{
		auto normalizeAngle = [](float angle) -> float
		{
			while (angle > 180.f) angle -= 360.f;
			while (angle < -180.f) angle += 360.f;
			return angle;
		};
		return Vec3(
			normalizeAngle(x - v.x),
			normalizeAngle(y - v.y),
			normalizeAngle(z - v.z)
		);
	}

	// Normalized - returns a normalized copy of this vector
	Vec3 Normalized() const
	{
		float len = Length();
		if (len > 0.0f)
			return Vec3(x / len, y / len, z / len);
		return Vec3(0.0f, 0.0f, 0.0f);
	}

	// Normalize2D - normalizes the 2D components (x, y) and returns the 2D length
	float Normalize2D()
	{
		float len = Length2D();
		if (len > 0.0f)
		{
			x /= len;
			y /= len;
		}
		else
		{
			x = y = 0.0f;
		}
		return len;
	}

	// LerpAngle - interpolates between angles properly handling wraparound
	inline Vec3 LerpAngle(const Vec3& v, float t) const
	{
		auto shortDist = [](const float flAngleA, const float flAngleB)
		{
			const float flDelta = fmodf((flAngleA - flAngleB), 360.f);
			return fmodf(2 * flDelta, 360.f) - flDelta;
		};
		return { x - shortDist(x, v.x) * t, y - shortDist(y, v.y) * t, z - shortDist(z, v.z) * t };
	}

	// Max - returns a vector with each component being the max of this and the given value
	Vec3 Max(float v) const
	{
		return Vec3(
			x > v ? x : v,
			y > v ? y : v,
			z > v ? z : v
		);
	}

	// Min - returns a vector with each component being the min of this and the given value
	Vec3 Min(float v) const
	{
		return Vec3(
			x < v ? x : v,
			y < v ? y : v,
			z < v ? z : v
		);
	}
};

class Vec2
{
public:
	float x = 0.0f, y = 0.0f;

public:
	Vec2(void)
	{
		x = y = 0.0f;
	}

	Vec2(float X, float Y)
	{
		x = X; y = Y;
	}

	Vec2(float *v)
	{
		x = v[0]; y = v[1];
	}

	Vec2(const float *v)
	{
		x = v[0]; y = v[1];
	}

	Vec2(const Vec2 &v)
	{
		x = v.x; y = v.y;
	}

	Vec2 &operator=(const Vec2 &v)
	{
		x = v.x; y = v.y; return *this;
	}

	float &operator[](int i)
	{
		return ((float *)this)[i];
	}

	float operator[](int i) const
	{
		return ((float *)this)[i];
	}

	Vec2 &operator+=(const Vec2 &v)
	{
		x += v.x; y += v.y; return *this;
	}

	Vec2 &operator-=(const Vec2 &v)
	{
		x -= v.x; y -= v.y; return *this;
	}

	Vec2 &operator*=(const Vec2 &v)
	{
		x *= v.x; y *= v.y; return *this;
	}

	Vec2 &operator/=(const Vec2 &v)
	{
		x /= v.x; y /= v.y; return *this;
	}

	Vec2 &operator+=(float v)
	{
		x += v; y += v; return *this;
	}

	Vec2 &operator-=(float v)
	{
		x -= v; y -= v; return *this;
	}

	Vec2 &operator*=(float v)
	{
		x *= v; y *= v; return *this;
	}

	Vec2 &operator/=(float v)
	{
		x /= v; y /= v; return *this;
	}

	Vec2 operator+(const Vec2 &v) const
	{
		return Vec2(x + v.x, y + v.y);
	}

	Vec2 operator-(const Vec2 &v) const
	{
		return Vec2(x - v.x, y - v.y);
	}

	Vec2 operator*(const Vec2 &v) const
	{
		return Vec2(x * v.x, y * v.y);
	}

	Vec2 operator/(const Vec2 &v) const
	{
		return Vec2(x / v.x, y / v.y);
	}

	Vec2 operator+(float v) const
	{
		return Vec2(x + v, y + v);
	}

	Vec2 operator-(float v) const
	{
		return Vec2(x - v, y - v);
	}

	Vec2 operator*(float v) const
	{
		return Vec2(x * v, y * v);
	}

	Vec2 operator/(float v) const
	{
		return Vec2(x / v, y / v);
	}

	void Set(float X = 0.0f, float Y = 0.0f)
	{
		x = X; y = Y;
	}

	float Length(void) const
	{
		return sqrtf(x * x + y * y);
	}

	float LengthSqr(void) const
	{
		return (x * x + y * y);
	}

	float DistTo(const Vec2 &v) const
	{
		return (*this - v).Length();
	}

	float DistToSqr(const Vec2 &v) const
	{
		return (*this - v).LengthSqr();
	}

	float Dot(const Vec2 &v) const
	{
		return (x * v.x + y * v.y);
	}

	bool IsZero(void) const
	{
		return (x > -0.01f && x < 0.01f &&
			y > -0.01f && y < 0.01f);
	}
};