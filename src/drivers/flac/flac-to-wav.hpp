#pragma once
#include <FLAC++/decoder.h>

#include "../../memfd.hpp"
#include "../../util/error.hpp"

namespace drivers::flac {
struct RiffChunkHeader {
    std::array<char, 4> id;
    uint32_t            size;
};

static_assert(sizeof(RiffChunkHeader) == 8);

struct WavHeader {
    RiffChunkHeader     riff_header; // "RIFF"
    std::array<char, 4> riff_tag;    // "WAVE"

    RiffChunkHeader wave_header; // "fmt "
    uint16_t        format;
    uint16_t        channels;
    uint32_t        sample_rate;
    uint32_t        bytes_per_sec;
    uint16_t        block_align;
    uint16_t        bits_per_sample;

    RiffChunkHeader data_header; // "data"
    // std::byte stream[];
} __attribute__((packed));

static_assert(sizeof(WavHeader) == 44);

constexpr auto riff_id(const char (&str)[5]) -> std::array<char, 4> {
    return {str[0], str[1], str[2], str[3]};
}

class Decoder : public FLAC::Decoder::File {
  private:
    struct Metadata {
        FLAC__uint64 total_samples;
        uint32_t     sample_rate;
        uint32_t     channels;
        uint32_t     bps;
    };

    std::optional<Metadata> metadata;
    FILE*                   output;

    auto write_wav_header(const Metadata& metadata) -> bool {
        const auto total_size = metadata.total_samples * metadata.channels * (metadata.bps / 8);
        const auto wav_header = WavHeader{
            .riff_header     = {riff_id("RIFF"), uint32_t(total_size + (sizeof(WavHeader) - sizeof(WavHeader::riff_header)))},
            .riff_tag        = riff_id("WAVE"),
            .wave_header     = {riff_id("fmt "), 16},
            .format          = 1, // format = PCM
            .channels        = uint16_t(metadata.channels),
            .sample_rate     = metadata.sample_rate,
            .bytes_per_sec   = metadata.channels * metadata.channels * (metadata.bps / 8),
            .block_align     = uint16_t(metadata.channels * (metadata.bps / 8)),
            .bits_per_sample = uint16_t(metadata.bps),
            .data_header     = {riff_id("data"), uint32_t(total_size)},
        };

        return fwrite(&wav_header, sizeof(WavHeader), 1, output) == 1;
    }

    auto write_callback(const FLAC__Frame* const frame, const FLAC__int32* const buffer[]) -> FLAC__StreamDecoderWriteStatus override {
        if(!metadata) {
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }

        for(auto i = uint32_t(0); i < frame->header.blocksize; i += 1) {
            for(auto c = uint32_t(0); c < metadata->channels; c += 1) {
                if(fwrite(&buffer[c][i], metadata->bps / 8, 1, output) != 1) {
                    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                }
            }
        }

        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    auto metadata_callback(const FLAC__StreamMetadata* const metadata) -> void override {
        switch(metadata->type) {
        case FLAC__METADATA_TYPE_STREAMINFO: {
            const auto& info = metadata->data.stream_info;
            const auto  m    = Metadata{
                    .total_samples = info.total_samples,
                    .sample_rate   = info.sample_rate,
                    .channels      = info.channels,
                    .bps           = info.bits_per_sample,
            };
            if(write_wav_header(m)) {
                this->metadata = m;
            }
        } break;
        default:
            break;
        }
    }
    auto error_callback(const FLAC__StreamDecoderErrorStatus /*status*/) -> void override {}

  public:
    Decoder(FILE* const output) : output(output) {}
};

inline auto flac_to_wav(const char* const path) -> int {
    auto input = File(fopen(path, "rb"));
    if(input == NULL) {
        return -1;
    }

    auto file = open_memory_file<OpenMode::Write>("decoded.wav");
    if(file == NULL) {
        return -1;
    }

    auto decoder = Decoder(file.get());
    if(decoder.init(input.get()) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        return -1;
    } else {
        [[maybe_unused]] const auto moved = input.release(); // 'input' was moved to decoder
    }

    if(!decoder.process_until_end_of_stream()) {
        return -1;
    }

    return extract_fd_from_file(file);
}
} // namespace drivers::flac
