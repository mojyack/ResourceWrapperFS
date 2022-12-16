#pragma once
#include "../../memfd.hpp"
#include "image.hpp"

namespace drivers::jxl {
struct BitmapFileHeader {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} __attribute__((packed));

struct BitmapInfoHeader {
    uint32_t biSize;
    uint32_t biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} __attribute__((packed));

constexpr auto BI_RGB = 0;

inline auto encode_bmp(const char* const filename, const Image<4>& image) -> int {
    auto file = open_memory_fd(filename);
    if(!file) {
        return -1;
    }

    const auto row_size = image.width * 4;
    const auto bytes    = row_size * image.height;

    auto file_header        = BitmapFileHeader();
    file_header.bfType      = ('M' << 8) | 'B';
    file_header.bfSize      = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + bytes;
    file_header.bfReserved1 = 0;
    file_header.bfReserved2 = 0;
    file_header.bfOffBits   = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader);

    auto info_header            = BitmapInfoHeader();
    info_header.biSize          = sizeof(BitmapInfoHeader);
    info_header.biWidth         = image.width;
    info_header.biHeight        = image.height;
    info_header.biPlanes        = 1;
    info_header.biBitCount      = 32;
    info_header.biCompression   = BI_RGB;
    info_header.biSizeImage     = 0;
    info_header.biXPelsPerMeter = 0x1274;
    info_header.biYPelsPerMeter = 0x1274;
    info_header.biClrUsed       = 0;
    info_header.biClrImportant  = 0;

    if(!file.write(file_header) || !file.write(info_header)) {
        return -1;
    }

    auto buf = std::vector<std::byte>();
    buf.reserve(image.height * image.width * 4);
    for(auto rr = size_t(0); rr < image.height; rr += 1) {
        const auto r   = image.height - rr - 1;
        const auto row = image.buffer.data() + r * row_size;
        for(auto c = size_t(0); c < image.width; c += 1) {
            const auto p = row + c * 4;
            buf.emplace_back(p[2]);
            buf.emplace_back(p[1]);
            buf.emplace_back(p[0]);
            buf.emplace_back(p[3]);
        }
    }
    if(!file.write(buf.data(), buf.size())) {
        return -1;
    }

    return file.release();
}
} // namespace drivers::jxl
