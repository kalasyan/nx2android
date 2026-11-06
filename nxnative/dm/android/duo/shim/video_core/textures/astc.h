// nx2android: Eden's software ASTC decoder (third_party/astc.cpp, as in belmont/)
#pragma once
#include <cstdint>
#include <span>
namespace Tegra::Texture::ASTC {
void Decompress(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t depth, uint32_t block_width, uint32_t block_height,
                std::span<uint8_t> output);
}
