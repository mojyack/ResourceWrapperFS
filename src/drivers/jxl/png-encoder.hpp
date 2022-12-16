#pragma once
#include <png.h>
#include <unistd.h>

#include "../../memfd.hpp"
#include "image.hpp"

namespace drivers::jxl {
inline auto encode_png(const char* const filename, const Image<4>& image) -> int {
    static_assert(sizeof(png_byte) == sizeof(uint8_t), "png_byte is not 8-bit");

    auto file = open_memory_fd(filename);
    if(!file) {
        return -1;
    }

    auto rows = png_bytepp(NULL);
    auto png  = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    auto info = png_create_info_struct(png);
    if(png == NULL || info == NULL) {
        goto end;
    }
    if(setjmp(png_jmpbuf(png))) {
        goto end;
    }

    struct WriteCallback {
        static auto write_callback(const png_structp png, const png_bytep data, const png_size_t length) -> void {
            const auto fd = static_cast<int>((uintptr_t)png_get_io_ptr(png));
            write(fd, data, length);
        }
    };

    png_set_write_fn(png, reinterpret_cast<void*>(file.as_handle()), WriteCallback::write_callback, NULL);
    png_set_IHDR(png, info, image.width, image.height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    rows = reinterpret_cast<png_bytepp>(png_malloc(png, sizeof(png_bytep) * image.height));
    if(rows == NULL) {
        goto end;
    }
    png_set_rows(png, info, rows);
    for(auto r = size_t(0); r < image.height; r += 1) {
        rows[r] = std::bit_cast<png_bytep>(image.buffer.data() + r * image.width * 4);
    }
    png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);

end:
    if(rows != NULL) {
        png_free(png, rows);
    }
    if(png != NULL) {
        png_destroy_write_struct(&png, &info);
    }
    return file.release();
}
} // namespace drivers::jxl
