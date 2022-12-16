#pragma once
#include <filesystem>
#include <string_view>

#include "../../driver.hpp"
#include "flac-to-wav.hpp"

namespace drivers::flac {
class Driver {
  private:
  public:
    auto get_real_path(const std::string_view path_str) const -> std::optional<std::string> {
        if(!path_str.ends_with(".wav")) {
            return std::nullopt;
        }

        auto real_path = std::filesystem::path(path_str).replace_extension(".flac");
        if(!std::filesystem::exists(real_path)) {
            return std::nullopt;
        }
        return real_path;
    }

    auto get_phantom_paths(const std::string_view path_str) const -> std::optional<std::vector<std::string>> {
        if(!path_str.ends_with(".flac")) {
            return std::nullopt;
        }

        auto       files = std::vector<std::string>();
        const auto path  = std::filesystem::path(path_str);
        for(const auto ext : {".wav"}) {
            auto file = std::filesystem::path(path).replace_extension(ext);
            files.emplace_back(file.string());
        }
        return files;
    }

    auto open_phantom_file(const std::string_view path_str) const -> std::optional<int> {
        auto require_wav = false;
        if(path_str.ends_with(".wav")) {
            require_wav = true;
        } else {
            return std::nullopt;
        }

        const auto real_path = std::filesystem::path(path_str).replace_extension(".flac");
        if(!std::filesystem::exists(real_path)) {
            return std::nullopt;
        }

        if(require_wav) {
            return flac_to_wav(real_path.c_str());
        }

        return -1;
    }
};

static_assert(::Driver<Driver>);
} // namespace drivers::flac
