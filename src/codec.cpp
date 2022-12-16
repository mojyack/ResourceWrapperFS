#include "drivers/flac/flac-to-wav.hpp"
#include "drivers/jxl/bmp-encoder.hpp"
#include "drivers/jxl/jpg-encoder.hpp"
#include "drivers/jxl/jxl-decoder.hpp"
#include "drivers/jxl/png-encoder.hpp"
#include "util/charconv.hpp"

template <class T>
auto do_not_optimize(const T& value) -> void {
    asm volatile(""
                 :
                 : "r,m"(value)
                 : "memory");
}

class CPUTimer {
  private:
    std::clock_t time;

  public:
    CPUTimer() {
        time = std::clock();
    }

    ~CPUTimer() {
        const auto now        = std::clock();
        const auto elapsed_ms = 1000.0 * (now - time) / CLOCKS_PER_SEC;
        std::cout << "Elapsed " << elapsed_ms << "ms" << std::endl;
    }
};

auto save_fd_to_file(const int fd, const char* const path) -> bool {
    const auto size = get_fd_size(fd);
    if(size == -1) {
        return false;
    }
    auto buf = std::vector<std::byte>(size);
    if(read(fd, buf.data(), buf.size()) != size) {
        printf("read() failed %d\n", errno);
        return false;
    }
    auto ofs = std::ofstream(path);
    ofs.write(std::bit_cast<const char*>(buf.data()), buf.size());
    return true;
}

auto main(const int argc, const char* const argv[]) -> int {
    if(argc < 4) {
        return 1;
    }

    const auto mode = std::string_view(argv[1]);
    if(mode == "a") { // jxl to jpg reconstruct test
        const auto file = drivers::jxl::decode_jxl_to_jpeg(argv[2]);
        if(!file) {
            puts(file.as_error().cstr());
            return 1;
        }
        const auto fd = file.as_value().as_handle();
        if(!save_fd_to_file(fd, argv[3])) {
            return 1;
        }
    } else if(mode == "b") { // jpg encoder test
        const auto image = drivers::jxl::decode_jxl<3>(argv[2]);
        if(!image) {
            puts(image.as_error().cstr());
            return 1;
        }
        const auto jpg = drivers::jxl::encode_jpg(argv[3], image.as_value());
        if(jpg == -1) {
            puts("jpg encode failed");
            return 1;
        }
        if(!save_fd_to_file(jpg, argv[3])) {
            puts("save failed");
            return 1;
        }
    } else if(mode == "c") { // jpg encode speed test
        const auto image = drivers::jxl::decode_jxl<3>(argv[2]);
        if(!image) {
            puts(image.as_error().cstr());
            return 1;
        }

        auto quality = int();
        if(const auto arg = from_chars<int>(argv[4]); !arg) {
            puts("invalid argument");
            return 1;
        } else {
            quality = *arg;
        }

        auto c     = 0;
        auto timer = CPUTimer();
        while(c++ < 50) {
            const auto jpg = drivers::jxl::encode_jpg(argv[3], image.as_value(), quality);
            do_not_optimize(jpg);
        }
    } else if(mode == "d") { // png encoder test
        const auto image = drivers::jxl::decode_jxl<4>(argv[2]);
        if(!image) {
            puts(image.as_error().cstr());
            return 1;
        }
        const auto png = drivers::jxl::encode_png(argv[3], image.as_value());
        if(png == -1) {
            puts("png encode failed");
            return 1;
        }
        if(!save_fd_to_file(png, argv[3])) {
            puts("save failed");
            return 1;
        }
    } else if(mode == "e") { // bmp encoder test
        const auto image = drivers::jxl::decode_jxl<4>(argv[2]);
        if(!image) {
            puts(image.as_error().cstr());
            return 1;
        }
        const auto bmp = drivers::jxl::encode_bmp(argv[3], image.as_value());
        if(bmp == -1) {
            puts("png encode failed");
            return 1;
        }
        if(!save_fd_to_file(bmp, argv[3])) {
            puts("save failed");
            return 1;
        }
    } else if(mode == "f") { // flac to wav test
        const auto wav = drivers::flac::flac_to_wav(argv[2]);
        if(wav == -1) {
            puts("flac to wav convert failed");
            return 1;
        }
        if(!save_fd_to_file(wav, argv[3])) {
            puts("save failed");
            return 1;
        }
    } else {
        puts("unknown mode");
        return 0;
    }
    return 0;
}
