#pragma once
#include <vector>

namespace drivers::jxl {
template <int channels>
struct Image {
    size_t                 width;
    size_t                 height;
    std::vector<std::byte> buffer;
};
} // namespace drivers::jxl
