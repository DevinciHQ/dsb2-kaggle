#ifndef GEOMETRY_VECTOR_H_
#define GEOMETRY_VECTOR_H_ 1

#include <cmath>

#include <kj/debug.h>

struct XYZ {
  XYZ(float x, float y, float z) : x(x), y(y), z(z) {}

  XYZ() = default;
  XYZ(const XYZ&) = default;
  XYZ& operator=(const XYZ&) = default;

  XYZ normalize() const {
    const auto scale = 1.0 / magnitude();
    return XYZ(x * scale, y * scale, z * scale);
  }

  float magnitude() const {
    return std::sqrt(squared_magnitude());
  }

  float squared_magnitude() const {
    return x * x + y * y + z * z;
  }

  XYZ operator*(float v) const {
    return XYZ(x * v, y * v, z * v);
  }

  XYZ operator/(float v) const {
    return *this * (1.0f / v);
  }

  float operator*(const XYZ& rhs) const {
    return x * rhs.x + y * rhs.y + z * rhs.z;
  }

  XYZ operator+(const XYZ& rhs) const {
    return XYZ(x + rhs.x, y + rhs.y, z + rhs.z);
  }

  XYZ operator-(const XYZ& rhs) const {
    return XYZ(x - rhs.x, y - rhs.y, z - rhs.z);
  }

  XYZ cross(const XYZ& rhs) const {
    return XYZ(
        y * rhs.z - z * rhs.y,
        z * rhs.x - x * rhs.z,
        x * rhs.y - y * rhs.x);
  }

  XYZ& operator+=(const XYZ& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;

    return *this;
  }

  XYZ& operator*=(float v) {
    x *= v;
    y *= v;
    z *= v;

    return *this;
  }

  float Get(size_t idx) const {
    switch (idx) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    default: KJ_FAIL_REQUIRE("Invalid axis", idx);
    }
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

#endif  // !GEOMETRY_VECTOR_H_
