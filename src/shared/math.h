#pragma once

struct Vec2f {
  float x, y;

  Vec2f() {}
  Vec2f(float x, float y) : x(x), y(y) {}
};

struct Vec3f {
  float x, y, z;

  Vec3f() {}
  Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct Vec4f {
  float x, y, z, w;

  Vec4f() {}
  Vec4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};
