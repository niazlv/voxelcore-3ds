// OGG Vorbis decoding for the 3DS port via stb_vorbis
// (the PC build uses libvorbis; here files are decoded fully into PCM).
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#include <memory>
#include <stdexcept>
#include <vector>

#include "audio/audio.hpp"
#include "coders/ogg.hpp"
#include "io/io.hpp"

std::unique_ptr<audio::PCM> ogg::load_pcm(
    const io::path& file, bool headerOnly
) {
    auto bytes = io::read_bytes_buffer(file);
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(
        reinterpret_cast<const unsigned char*>(bytes.data()),
        bytes.size(), &error, nullptr);
    if (vorbis == nullptr) {
        throw std::runtime_error(
            "could not decode ogg file '" + file.string() + "'");
    }
    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    unsigned int totalSamples = stb_vorbis_stream_length_in_samples(vorbis);

    std::vector<char> data;
    if (!headerOnly) {
        data.resize(size_t(totalSamples) * info.channels * sizeof(short));
        int decoded = stb_vorbis_get_samples_short_interleaved(
            vorbis, info.channels,
            reinterpret_cast<short*>(data.data()),
            totalSamples * info.channels);
        data.resize(size_t(decoded) * info.channels * sizeof(short));
        totalSamples = decoded;
    }
    stb_vorbis_close(vorbis);

    return std::make_unique<audio::PCM>(
        std::move(data),
        totalSamples,
        static_cast<uint8_t>(info.channels),
        16,
        info.sample_rate,
        true);
}

std::unique_ptr<audio::PCMStream> ogg::create_stream(const io::path&) {
    // streamed vorbis is not supported on 3DS (short sounds only)
    return nullptr;
}
