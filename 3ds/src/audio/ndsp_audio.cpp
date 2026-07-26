// NDSP (3DS DSP) backend for the VoxelCore audio system.
// Sounds are fully decoded PCM copied into linear memory; each Speaker
// occupies one of the 23 ndsp channels. 3D positioning is approximated
// with distance attenuation + stereo panning against the listener.
#include <3ds.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "audio/audio.hpp"
#include "audio/NoAudio.hpp"
#include "debug/Logger.hpp"

static debug::Logger logger("ndsp-audio");

namespace audio {

namespace {
    constexpr int NDSP_CHANNELS = 23;  // channel 23 reserved
    bool channelBusy[NDSP_CHANNELS] = {};

    glm::vec3 listenerPos(0.0f);
    glm::vec3 listenerRight(1.0f, 0.0f, 0.0f);

    int allocChannel() {
        for (int i = 0; i < NDSP_CHANNELS; i++) {
            if (!channelBusy[i]) {
                channelBusy[i] = true;
                return i;
            }
        }
        return -1;
    }

    void freeChannel(int id) {
        if (id >= 0 && id < NDSP_CHANNELS) {
            ndspChnWaveBufClear(id);
            channelBusy[id] = false;
        }
    }

    struct SharedSample {
        void* linear = nullptr;    // linearAlloc'd 16-bit PCM
        size_t nsamples = 0;       // samples per channel
        int channels = 1;
        float sampleRate = 44100.0f;

        ~SharedSample() {
            if (linear) {
                linearFree(linear);
            }
        }
    };
}

class NDSPSpeaker : public Speaker {
    std::shared_ptr<SharedSample> sample;
    int channel;       // ndsp channel, -1 if none
    int audioChannel;  // engine mixer channel index
    int priority;
    ndspWaveBuf waveBuf {};
    float volume = 1.0f;
    float channelVolume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool relative = false;
    bool paused = false;
    bool manuallyStopped = false;
    glm::vec3 position {};
    glm::vec3 velocity {};

    void applyMix() {
        if (channel < 0) return;
        float gain = volume * channelVolume;
        float pan = 0.0f;
        if (!relative) {
            glm::vec3 delta = position - listenerPos;
            float dist = glm::length(delta);
            gain /= (1.0f + dist * 0.25f);
            if (dist > 0.001f) {
                pan = glm::dot(delta / dist, listenerRight) * 0.6f;
            }
        }
        float mix[12] = {};
        mix[0] = gain * (1.0f - glm::max(0.0f, pan));   // front left
        mix[1] = gain * (1.0f + glm::min(0.0f, pan));   // front right
        ndspChnSetMix(channel, mix);
    }

public:
    NDSPSpeaker(
        std::shared_ptr<SharedSample> sample,
        int channel,
        int priority,
        int audioChannel
    )
        : sample(std::move(sample)),
          channel(channel),
          audioChannel(audioChannel),
          priority(priority) {
    }

    ~NDSPSpeaker() override {
        if (channel >= 0) {
            freeChannel(channel);
            channel = -1;
        }
    }

    void update(const Channel* ch) override {
        if (ch) {
            channelVolume = ch->getVolume();
        }
        if (channel >= 0) {
            applyMix();
        }
    }

    int getChannel() const override {
        return audioChannel;
    }

    State getState() const override {
        if (channel < 0) return State::stopped;
        if (paused) return State::paused;
        if (waveBuf.status == NDSP_WBUF_DONE) return State::stopped;
        return State::playing;
    }

    float getVolume() const override { return volume; }
    void setVolume(float v) override {
        volume = v;
        applyMix();
    }
    float getPitch() const override { return pitch; }
    void setPitch(float p) override {
        pitch = p;
        if (channel >= 0) {
            ndspChnSetRate(channel, sample->sampleRate * pitch);
        }
    }
    bool isLoop() const override { return loop; }
    void setLoop(bool flag) override {
        loop = flag;
        waveBuf.looping = flag;
    }

    void play() override {
        if (channel < 0) return;
        if (paused) {
            ndspChnSetPaused(channel, false);
            paused = false;
            return;
        }
        ndspChnReset(channel);
        ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
        ndspChnSetRate(channel, sample->sampleRate * pitch);
        ndspChnSetFormat(
            channel,
            sample->channels == 2 ? NDSP_FORMAT_STEREO_PCM16
                                  : NDSP_FORMAT_MONO_PCM16);
        std::memset(&waveBuf, 0, sizeof(waveBuf));
        waveBuf.data_vaddr = sample->linear;
        waveBuf.nsamples = sample->nsamples;
        waveBuf.looping = loop;
        applyMix();
        ndspChnWaveBufAdd(channel, &waveBuf);
        paused = false;
    }

    void pause() override {
        if (channel >= 0) {
            ndspChnSetPaused(channel, true);
            paused = true;
        }
    }

    void stop() override {
        manuallyStopped = true;
        if (channel >= 0) {
            freeChannel(channel);
            channel = -1;
        }
    }

    duration_t getTime() const override {
        if (channel < 0) return 0.0;
        return ndspChnGetSamplePos(channel) / (double)sample->sampleRate;
    }
    duration_t getDuration() const override {
        return sample->nsamples / (double)sample->sampleRate;
    }
    void setTime(duration_t) override {}

    void setPosition(glm::vec3 pos) override {
        position = pos;
        applyMix();
    }
    glm::vec3 getPosition() const override { return position; }
    void setVelocity(glm::vec3 vel) override { velocity = vel; }
    glm::vec3 getVelocity() const override { return velocity; }
    int getPriority() const override { return priority; }
    void setRelative(bool flag) override {
        relative = flag;
        applyMix();
    }
    bool isRelative() const override { return relative; }
    bool isManuallyStopped() const override { return manuallyStopped; }

    // resume from Channel::resume
    friend class NDSPAudio;
};

class NDSPSound : public Sound {
    std::shared_ptr<PCM> pcm;
    std::shared_ptr<SharedSample> sample;
    duration_t duration;

public:
    NDSPSound(std::shared_ptr<PCM> pcmData, bool keepPCM) {
        duration = pcmData->getDuration();
        auto shared = std::make_shared<SharedSample>();
        shared->channels = pcmData->channels;
        shared->sampleRate = pcmData->sampleRate;

        // convert to 16-bit PCM in linear (DSP-visible) memory
        if (pcmData->bitsPerSample == 16) {
            size_t bytes = pcmData->data.size();
            shared->linear = linearAlloc(bytes);
            if (shared->linear) {
                std::memcpy(shared->linear, pcmData->data.data(), bytes);
                shared->nsamples =
                    bytes / 2 / glm::max(1, (int)pcmData->channels);
            }
        } else if (pcmData->bitsPerSample == 8) {
            size_t count = pcmData->data.size();
            shared->linear = linearAlloc(count * 2);
            if (shared->linear) {
                int16_t* dst = reinterpret_cast<int16_t*>(shared->linear);
                const uint8_t* src =
                    reinterpret_cast<const uint8_t*>(pcmData->data.data());
                for (size_t i = 0; i < count; i++) {
                    dst[i] = (int16_t(src[i]) - 128) << 8;
                }
                shared->nsamples =
                    count / glm::max(1, (int)pcmData->channels);
            }
        }
        if (shared->linear) {
            DSP_FlushDataCache(shared->linear, shared->nsamples * 2 *
                               shared->channels);
            sample = std::move(shared);
        }
        if (keepPCM) {
            pcm = std::move(pcmData);
        }
    }

    duration_t getDuration() const override { return duration; }
    std::shared_ptr<PCM> getPCM() const override { return pcm; }

    std::unique_ptr<Speaker> newInstance(int priority, int channel)
        const override {
        if (sample == nullptr) {
            return nullptr;
        }
        int ndspChannel = allocChannel();
        if (ndspChannel < 0) {
            return nullptr;
        }
        return std::make_unique<NDSPSpeaker>(
            sample, ndspChannel, priority, channel);
    }
};

class NDSPAudio : public Backend {
public:
    ~NDSPAudio() override {
        ndspExit();
    }

    std::unique_ptr<Sound> createSound(
        std::shared_ptr<PCM> pcm, bool keepPCM
    ) override {
        return std::make_unique<NDSPSound>(std::move(pcm), keepPCM);
    }

    std::unique_ptr<Stream> openStream(
        std::shared_ptr<PCMStream> stream, bool keepSource
    ) override {
        // no streamed music on 3DS (v1): dummy stream
        return std::make_unique<NoStream>(stream, keepSource);
    }

    std::unique_ptr<InputDevice> openInputDevice(
        const std::string&, uint, uint, uint
    ) override {
        return nullptr;
    }

    std::vector<std::string> getInputDeviceNames() override { return {}; }
    std::vector<std::string> getOutputDeviceNames() override {
        return {"ndsp"};
    }

    void setListener(
        glm::vec3 position, glm::vec3, glm::vec3 lookAt, glm::vec3 up
    ) override {
        listenerPos = position;
        glm::vec3 front = lookAt - position;
        if (glm::length(front) > 0.001f) {
            front = glm::normalize(front);
            listenerRight = glm::normalize(glm::cross(front, up));
        }
    }

    void update(double) override {}
    void setAcoustics(Acoustics) override {}
    bool isDummy() const override { return false; }

    static std::unique_ptr<NDSPAudio> create() {
        if (R_FAILED(ndspInit())) {
            // no DSP firmware dump on this console
            logger.error() << "ndspInit failed (missing dspfirm.cdc?)";
            return nullptr;
        }
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspSetMasterVol(1.0f);
        logger.info() << "NDSP audio initialized";
        return std::make_unique<NDSPAudio>();
    }
};

Backend* create_ndsp_backend() {
    auto backend = NDSPAudio::create();
    return backend.release();
}

}  // namespace audio
