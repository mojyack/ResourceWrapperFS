#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "../../driver.hpp"
#include "bmp-encoder.hpp"
#include "jpg-encoder.hpp"
#include "jxl-decoder.hpp"
#include "png-encoder.hpp"

namespace drivers::jxl {
class Driver {
  private:
  public:
    auto get_real_path(const std::string_view path_str) const -> std::optional<std::string> {
        if(!path_str.ends_with(".jpg") && !path_str.ends_with(".png") && !path_str.ends_with(".bmp")) {
            return std::nullopt;
        }

        auto real_path = std::filesystem::path(path_str).replace_extension(".jxl");
        if(!std::filesystem::exists(real_path)) {
            return std::nullopt;
        }
        return real_path;
    }

    auto get_phantom_paths(const std::string_view path_str) const -> std::optional<std::vector<std::string>> {
        if(!path_str.ends_with(".jxl")) {
            return std::nullopt;
        }

        auto       files = std::vector<std::string>();
        const auto path  = std::filesystem::path(path_str);
        for(const auto ext : {".bmp", ".png", ".jpg"}) {
            auto file = std::filesystem::path(path).replace_extension(ext);
            files.emplace_back(file.string());
        }
        return files;
    }

    auto open_phantom_file(const std::string_view path_str) const -> std::optional<int> {
        auto require_jpg = false;
        auto require_png = false;
        auto require_bmp = false;
        if(path_str.ends_with(".jpg")) {
            require_jpg = true;
        } else if(path_str.ends_with(".png")) {
            require_png = true;
        } else if(path_str.ends_with(".bmp")) {
            require_bmp = true;
        } else {
            return std::nullopt;
        }

        const auto real_path = std::filesystem::path(path_str).replace_extension(".jxl");
        if(!std::filesystem::exists(real_path)) {
            return std::nullopt;
        }

        if(require_jpg) {
            if(auto reconstructed = decode_jxl_to_jpeg(real_path.c_str())) {
                return reconstructed.as_value().release();
            }

            const auto bytes = decode_jxl<3>(real_path.c_str());
            if(!bytes) {
                return -1;
            }
            return encode_jpg("encoded", bytes.as_value());
        } else if(require_png) {
            const auto bytes = decode_jxl<4>(real_path.c_str());
            if(!bytes) {
                return -1;
            }
            return encode_png("encoded", bytes.as_value());
        } else if(require_bmp) {
            const auto bytes = decode_jxl<4>(real_path.c_str());
            if(!bytes) {
                return -1;
            }
            return encode_bmp("encoded", bytes.as_value());
        }

        return -1;
    }
};

static_assert(::Driver<Driver>);
} // namespace drivers::jxl
