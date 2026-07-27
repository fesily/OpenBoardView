#pragma once
namespace obv {
struct Vec2 {
  float x = 0.f;
  float y = 0.f;
  Vec2() = default;
  Vec2(float x, float y) : x(x), y(y) {}
};
} // namespace obv
