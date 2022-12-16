#pragma once
#include <stdio.h>

#include <jpeglib.h>
#include <setjmp.h>

#include "../../memfd.hpp"
#include "image.hpp"

namespace drivers::jxl {
class Jpeg {
  private:
    jpeg_compress_struct jpeg;

  public:
    auto operator->() -> jpeg_compress_struct* {
        return &jpeg;
    }

    operator jpeg_compress_struct*() {
        return &jpeg;
    }

    Jpeg() {
        jpeg_create_compress(&jpeg);
    }

    ~Jpeg() {
        jpeg_destroy_compress(&jpeg);
    }
};

struct ErrorManager {
    jpeg_error_mgr jerr;
    jmp_buf        jmpbuf;

    static void error_exit(const j_common_ptr cinfo) {
        auto& self = *std::bit_cast<ErrorManager*>(cinfo->err);
        longjmp(self.jmpbuf, 1);
    }

    ErrorManager() {
        jerr.error_exit = error_exit;
    }
};

inline auto encode_jpg(const char* const filename, const Image<3>& image, const int quality = 75) -> int {
    auto file = open_memory_file<OpenMode::Write>(filename);
    if(file == NULL) {
        return -1;
    }

    auto jpeg = Jpeg();
    auto em   = ErrorManager();
    jpeg->err = jpeg_std_error(&em.jerr);
    if(setjmp(em.jmpbuf)) {
        return -1;
    }

    jpeg_stdio_dest(jpeg, file.get());
    jpeg->image_width      = image.width;
    jpeg->image_height     = image.height;
    jpeg->input_components = 3;
    jpeg->in_color_space   = JCS_RGB;
    jpeg_set_defaults(jpeg);
    jpeg_set_quality(jpeg, quality, TRUE);
    jpeg_start_compress(jpeg, TRUE);
    for(auto i = size_t(0); i < image.height; i += 1) {
        const auto rows = image.buffer.data() + image.width * i * 3;
        jpeg_write_scanlines(jpeg, std::bit_cast<JSAMPROW*>(&rows), 1);
    }
    jpeg_finish_compress(jpeg);
    return extract_fd_from_file(file);
}
} // namespace drivers::jxl
