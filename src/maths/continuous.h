#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "maths/fixed.h"
#include "maths/integer.h"

namespace corncob::maths {

// Fractions belong to the live simulation, never to an interpolated display snapshot.
struct motion_fraction {
  vec3<double> position{0, 0, 0};
  vec3<double> velocity{0, 0, 0};
  vec3<double> angles{0, 0, 0};
};
inline int64_t accumulate(double const amount, double &fraction) {
  double const total{amount + fraction};
  auto const whole{static_cast<int64_t>(std::trunc(total))};
  fraction = total - static_cast<double>(whole);
  return whole;
}
inline void move(world_position &position, vec3<double> const &distance, vec3<double> &fraction) {
  for(unsigned int axis{0}; axis < 3; ++axis) position[axis] = dword(static_cast<int64_t>(position[axis]) + accumulate(distance[axis], fraction[axis]));
}
inline void accelerate(fixed_vector &velocity, vec3<double> const &change, vec3<double> &fraction) {
  for(unsigned int axis{0}; axis < 3; ++axis) velocity[axis] = word(dword(static_cast<int64_t>(velocity[axis]) + accumulate(change[axis], fraction[axis])));
}
inline void turn(angle &value, double const amount, double &fraction) {
  value = static_cast<angle>(value + accumulate(amount, fraction));
}
inline double decay(double const retained, double const reference_steps) {
  return std::pow(retained, reference_steps);
}
struct real_rotation {
  std::array<vec3<double>, 3> rows;
  vec3<double> apply(vec3<double> const &v) const {
    return {rows[0].dot(v), rows[1].dot(v), rows[2].dot(v)};
  }
  vec3<double> inverse(vec3<double> const &v) const {
    return {rows[0].x*v.x + rows[1].x*v.y + rows[2].x*v.z,
            rows[0].y*v.x + rows[1].y*v.y + rows[2].y*v.z,
            rows[0].z*v.x + rows[1].z*v.y + rows[2].z*v.z};
  }
};
inline real_rotation rotation_real(vec3<double> const &angles) {
  double constexpr radians{6.2831853071795864769 / 65536.0};
  double const h{angles.x*radians}, p{angles.y*radians}, r{angles.z*radians};
  double const ch{std::cos(h)}, sh{std::sin(h)}, cp{std::cos(p)}, sp{std::sin(p)}, cr{std::cos(r)}, sr{std::sin(r)};
  return {{{{ch*cp, ch*sp*sr-sh*cr, ch*sp*cr+sh*sr},
             {sh*cp, sh*sp*sr+ch*cr, sh*sp*cr-ch*sr}, {-sp, cp*sr, cp*cr}}}};
}
inline vec3<double> real_angles(angles const &value, vec3<double> const &fraction = {0, 0, 0}) {
  return {value.heading + fraction.x, value.pitch + fraction.y, value.roll + fraction.z};
}
inline void rotate_elapsed(angles &value, vec3<double> &fraction, vec3<double> const &change) {
  auto const a{rotation_real(real_angles(value, fraction))}, b{rotation_real(change)};
  real_rotation result{{vec3<double>{0,0,0}, vec3<double>{0,0,0}, vec3<double>{0,0,0}}};
  for(unsigned int row{0}; row < 3; ++row) for(unsigned int col{0}; col < 3; ++col)
    for(unsigned int k{0}; k < 3; ++k) result.rows[row][col] += a.rows[row][k] * b.rows[k][col];
  double constexpr units{65536.0 / 6.2831853071795864769};
  double const pitch{std::asin(std::clamp(-result.rows[2].x, -1.0, 1.0))};
  vec3<double> next{std::atan2(result.rows[1].x, result.rows[0].x)*units,
                    pitch*units, std::atan2(result.rows[2].y, result.rows[2].z)*units};
  if(std::abs(std::cos(pitch)) < 1e-8) { next.x = std::atan2(-result.rows[0].y, result.rows[1].y)*units; next.z = 0; }
  std::array<angle*,3> const out{&value.heading, &value.pitch, &value.roll};
  for(unsigned int axis{0}; axis < 3; ++axis) {
    auto const integer{static_cast<int64_t>(std::trunc(next[axis]))};
    *out[axis] = static_cast<angle>(integer);
    fraction[axis] = next[axis] - static_cast<double>(integer);
  }
}

} // namespace corncob::maths
