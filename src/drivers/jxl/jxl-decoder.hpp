#pragma once
#include <filesystem>
#include <fstream>
#include <string_view>

#include <jxl/decode_cxx.h>
#include <jxl/encode_cxx.h>

#include "../../memfd.hpp"
#include "../../util/misc.hpp"
#include "image.hpp"

namespace drivers::jxl {
template <int channels>
auto decode_jxl(const char* const path) -> Result<Image<channels>> {
    const auto file_result = read_binary(path);
    if(!file_result) {
        return file_result.as_error();
    }
    const auto& file = file_result.as_value();

    const auto decoder = JxlDecoderMake(NULL);

    if(JxlDecoderSetInput(decoder.get(), std::bit_cast<uint8_t*>(file.data()), file.size()) != JXL_DEC_SUCCESS) {
        return Error("jxl: failed to set input");
    }

    auto              info   = JxlBasicInfo();
    const static auto format = JxlPixelFormat{.num_channels = channels, .data_type = JxlDataType::JXL_TYPE_UINT8, .endianness = JxlEndianness::JXL_NATIVE_ENDIAN, .align = 1};
    auto              buffer = std::vector<std::byte>();

    if(JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
        return Error("jxl: failed to subscribe events");
    }

    while(true) {
        switch(JxlDecoderProcessInput(decoder.get())) {
        case JXL_DEC_ERROR:
            return Error("jxl: decoder error");
        case JXL_DEC_NEED_MORE_INPUT:
            return Error("jxl: no more inputs");
        case JXL_DEC_BASIC_INFO:
            if(JxlDecoderGetBasicInfo(decoder.get(), &info) != JXL_DEC_SUCCESS) {
                return Error("jxl: failed to get basic info");
            }
            break;
        case JXL_DEC_COLOR_ENCODING:
            continue;
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
            auto buffer_size = size_t();
            if(JxlDecoderImageOutBufferSize(decoder.get(), &format, &buffer_size)) {
                return Error("jxl: failed to get output buffer size");
            }

            buffer.resize(buffer_size);
            if(JxlDecoderSetImageOutBuffer(decoder.get(), &format, buffer.data(), buffer.size()) != JXL_DEC_SUCCESS) {
                return Error("jxl: failed to set output buffer");
            }
        } break;
        case JXL_DEC_FULL_IMAGE:
            break;
        case JXL_DEC_SUCCESS:
            goto finish;
            break;
        default:
            return Error("jxl: unknown state");
        }
    }

finish:
    return Image<channels>{info.xsize, info.ysize, std::move(buffer)};
}

inline auto decode_jxl_to_jpeg(const char* const path) -> Result<FileDescriptor> {
    const auto file_result = read_binary(path);
    if(!file_result) {
        return file_result.as_error();
    }
    const auto& file = file_result.as_value();

    const auto decoder         = JxlDecoderMake(NULL);
    auto       jpeg_data_chunk = std::vector<std::byte>(16384);
    auto       decoded         = open_memory_fd(std::filesystem::path(path).filename().c_str());
    if(!decoded) {
        return Error("failed to open temporary file");
    }

    const auto write_decoded = [&decoder, &jpeg_data_chunk, &decoded]() -> Result<size_t> {
        const auto used_jpeg_output = jpeg_data_chunk.size() - JxlDecoderReleaseJPEGBuffer(decoder.get());
        if(used_jpeg_output == 0) {
            return size_t(used_jpeg_output);
        }

        if(!decoded.write(jpeg_data_chunk.data(), jpeg_data_chunk.size())) {
            return Error("jxl: failed to write decoded buffer");
        }
        return size_t(used_jpeg_output);
    };

    if(JxlDecoderSetInput(decoder.get(), std::bit_cast<uint8_t*>(file.data()), file.size()) != JXL_DEC_SUCCESS) {
        return Error("jxl: failed to set input");
    }

    if(JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_FULL_IMAGE | JXL_DEC_JPEG_RECONSTRUCTION) != JXL_DEC_SUCCESS) {
        return Error("jxl: failed to subscribe events");
    }

    while(true) {
        switch(JxlDecoderProcessInput(decoder.get())) {
        case JXL_DEC_ERROR:
            return Error("jxl: decoder error");
        case JXL_DEC_NEED_MORE_INPUT:
            return Error("jxl: no more inputs");
        case JXL_DEC_JPEG_RECONSTRUCTION:
            if(JxlDecoderSetJPEGBuffer(decoder.get(), std::bit_cast<uint8_t*>(jpeg_data_chunk.data()), jpeg_data_chunk.size()) != JXL_DEC_SUCCESS) {
                return Error("jxl: failed to set JPEG buffer");
            }
            break;
        case JXL_DEC_JPEG_NEED_MORE_OUTPUT: {
            if(const auto used_size = write_decoded(); !used_size) {
                return used_size.as_error();
            } else if(used_size.as_value() == 0) {
                jpeg_data_chunk.resize(jpeg_data_chunk.size() * 2);
            }
            if(JxlDecoderSetJPEGBuffer(decoder.get(), std::bit_cast<uint8_t*>(jpeg_data_chunk.data()), jpeg_data_chunk.size()) != JXL_DEC_SUCCESS) {
                return Error("jxl: failed to set JPEG buffer");
            }
        } break;
        case JXL_DEC_FULL_IMAGE:
            break;
        case JXL_DEC_SUCCESS:
            goto finish;
            break;
        default:
            return Error("jxl: unknown state");
        }
    }
finish:
    if(const auto used_size = write_decoded(); !used_size) {
        return used_size.as_error();
    }
    return decoded;
}
} // namespace drivers::jxl
