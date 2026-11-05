// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace belmont_map_decoration {
inline float Smooth(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}
struct Water {
  float collision_range{}, collision_contrast{}, thickness{}, contrast{};
};
// Static native material contribution. Alpha is a distance field, never display
// opacity; B supplies the collision gate. Noise is the material-weighted global
// noise attenuation. Scene reveal is applied by the caller after composition.
inline float WaterMask(float alpha, float collision, float noise,
                       const Water &p) {
  if (!std::isfinite(alpha) || !std::isfinite(collision) ||
      !std::isfinite(noise) || !std::isfinite(p.collision_range) ||
      !std::isfinite(p.collision_contrast) || !std::isfinite(p.thickness) ||
      !std::isfinite(p.contrast) || p.collision_contrast >= 1.f ||
      p.contrast >= 1.f)
    return 0.f;
  const float extent = Smooth((collision - p.collision_range) /
                              (1.f - p.collision_contrast));
  const float fill = Smooth((alpha - p.thickness) / (1.f - p.contrast));
  return extent * fill * (1.f - .25f * noise);
}
// Material's triangular water displacement frozen at time zero. No clock,
// guest state or fabricated animation is needed to retain the native shoreline.
inline float WaterOffset(float world_x, float repetitions, float strength) {
  const float phase = world_x * .5f * repetitions;
  const float triangle = 1.f - std::abs(2.f * (phase - std::floor(phase)) - 1.f);
  return (2.f * Smooth(triangle) - 1.5f) * strength * 2.f;
}
} // namespace belmont_map_decoration

namespace belmont_map_decoration {
using Noise = std::array<float, 4>;
struct Lines {
  float global_range{}, main_range{}, main_thickness{}, main_contrast{};
  float second_range{}, second_thickness{}, second_contrast{};
  float main_noise_strength{}, main_noise_contrast{};
  float second_noise_strength{}, second_noise_contrast{};
  Noise displacement{};
  float global_contrast{}, gradient_range{}, gradient_contrast{};
  float perlin_range{}, perlin_contrast{};
};
inline float Gate(float numerator, float denominator) {
  if (denominator == 0.f) return numerator > 0.f ? 1.f : 0.f;
  return Smooth(numerator / denominator);
}
inline float MeanRgb(const Noise &n) { return (n[0] + n[1] + n[2]) / 3.f; }
inline Noise Blend(const Noise &a, const Noise &b, float t) {
  Noise out{};
  for (unsigned i = 0; i < 4; ++i) out[i] = std::lerp(a[i], b[i], t);
  return out;
}
inline float Shift(const Noise &n, const Noise &weights, float factor) {
  float out = 0.f;
  for (unsigned i = 0; i < 4; ++i) out += (n[i] - .5f) * weights[i];
  return std::min(out * factor, 1.f);
}
struct Ring {
  float lower{}, upper{};
  float Value() const { return (1.f - lower) * upper; }
};
inline Ring Contour(float distance, float center, float thickness,
                    float contrast) {
  const float half_width = thickness + .49f * contrast - .5f;
  const float width = std::min(contrast, .99f) - 1.f;
  return {Gate(distance - (center - half_width), width),
          Gate(distance - (center + half_width), width)};
}
// Four native SDF contour bands, with local-UV noise frozen at time zero.
// Shared lower-band gates preserve the native overlap suppression when noise
// shifts adjacent bands over each other; this is not a generic wall outline.
inline float LineSignal(float distance, const Noise &a, const Noise &b,
                        const Noise &c, const Lines &p) {
  const auto ab = Blend(a, b, .5f), ac = Blend(a, c, .5f);
  const auto near_noise = Blend(ac, ab, .25f), middle_noise = Blend(ac, ab, .75f);
  const float center = 1.f + p.global_range;
  const auto main = Contour(distance, center + p.main_range + Shift(ac, p.displacement, 2.f),
                            p.main_thickness, p.main_contrast);
  const auto near = Contour(distance, center + p.second_range + Shift(near_noise, p.displacement, 2.f),
                            p.second_thickness, p.second_contrast);
  const auto middle = Contour(distance, center + 2.f * p.second_range + Shift(middle_noise, p.displacement, 1.5f),
                            p.second_thickness, p.second_contrast);
  const auto far = Contour(distance, center + 4.f * p.second_range + Shift(ab, p.displacement, 1.f),
                            p.second_thickness, p.second_contrast);
  const float second_width = 1.f - p.second_noise_contrast;
  const float main_density = Gate(MeanRgb(ac) - p.main_noise_strength,
                                  1.f - p.main_noise_contrast);
  const float near_density = Gate(MeanRgb(near_noise) - p.second_noise_strength, second_width);
  const float middle_density = Gate(MeanRgb(middle_noise) - p.second_noise_strength - .1f, second_width);
  const float far_density = Gate(MeanRgb(ab) - p.second_noise_strength - .1f, second_width);
  return std::max({main.Value() * main_density,
      near.Value() * near_density * (1.f - main.lower),
      middle.Value() * middle_density * (1.f - near.lower),
      far.Value() * far_density * (1.f - near.lower)});
}
inline float LineFactor(float distance, const Noise &a, const Noise &b,
                        const Noise &c, const Noise &perlin, const Lines &p) {
  const float reveal = Gate(1.f + p.global_range + Shift(Blend(a, c, .5f), p.displacement, 2.f) - distance,
                           1.f - std::min(p.global_contrast, .99f));
  const float gradient = .25f * Gate(1.f + p.global_range + p.gradient_range - distance,
                                    1.f - std::min(p.gradient_contrast, .99f)) * (1.f - reveal);
  const float perlin_alpha = Smooth(perlin[3] * 6.66796875f);
  const float sparkle = c[2] * (1.f - Gate(perlin[2] - p.perlin_range,
                                         1.f - p.perlin_contrast)) * perlin_alpha * perlin_alpha;
  return std::max(LineSignal(distance, a, b, c, p), gradient * sparkle);
}
} // namespace belmont_map_decoration
