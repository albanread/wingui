#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "wingui/wingui.h"

#include "wingui_internal.h"

#include "SynthEngine.h"
#include "SoundBank.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using SuperTerminal::SoundBank;

constexpr uint32_t kSampleRate = 44100;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kFramesPerBlock = 2048;
constexpr uint32_t kMixerBlocks = 4;

struct ActiveVoice {
    uint32_t sound_id = 0;
    size_t frame_pos = 0;
    float gain_left = 1.0f;
    float gain_right = 1.0f;
};

struct MixerBlock {
    WAVEHDR header{};
    std::vector<int16_t> samples;
};

struct AudioRuntime {
    std::mutex mutex;
    SynthEngine synth;
    SoundBank bank;
    HWAVEOUT wave_out = nullptr;
    HMIDIOUT midi_out = nullptr;
    std::vector<MixerBlock> blocks;
    std::vector<ActiveVoice> voices;
    std::thread worker;
    bool shutdown_requested = false;
    bool audio_initialized = false;
    bool midi_initialized = false;
    float master_volume = 1.0f;
};

AudioRuntime g_audio;

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float clamp11(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

WaveformType waveformTypeFromCode(int code) {
    switch (code) {
    case WINGUI_AUDIO_WAVEFORM_SQUARE: return WaveformType::SQUARE;
    case WINGUI_AUDIO_WAVEFORM_SAWTOOTH: return WaveformType::SAWTOOTH;
    case WINGUI_AUDIO_WAVEFORM_TRIANGLE: return WaveformType::TRIANGLE;
    case WINGUI_AUDIO_WAVEFORM_NOISE: return WaveformType::NOISE;
    case WINGUI_AUDIO_WAVEFORM_PULSE: return WaveformType::PULSE;
    default: return WaveformType::SINE;
    }
}

void normalizeBuffer(SynthAudioBuffer& buffer, float target = 0.95f) {
    float peak = 0.0f;
    for (float sample : buffer.samples) {
        peak = std::max(peak, std::fabs(sample));
    }
    if (peak <= 0.0f) {
        return;
    }
    const float scale = peak > target ? (target / peak) : 1.0f;
    if (scale == 1.0f) {
        return;
    }
    for (float& sample : buffer.samples) {
        sample *= scale;
    }
}

void applySimpleFilter(SynthAudioBuffer& buffer, int filter_type, float cutoff_hz, float resonance) {
    (void)resonance;
    if (filter_type <= WINGUI_AUDIO_FILTER_NONE || buffer.channels == 0 || cutoff_hz <= 0.0f) {
        return;
    }

    const float dt = 1.0f / static_cast<float>(buffer.sampleRate);
    const float rc = 1.0f / (2.0f * 3.14159265358979323846f * std::max(cutoff_hz, 20.0f));
    const float low_alpha = dt / (rc + dt);
    const float high_alpha = rc / (rc + dt);

    std::vector<float> low_prev(buffer.channels, 0.0f);
    std::vector<float> high_prev(buffer.channels, 0.0f);
    std::vector<float> input_prev(buffer.channels, 0.0f);

    for (size_t frame = 0; frame < buffer.getFrameCount(); ++frame) {
        for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
            const size_t index = frame * buffer.channels + ch;
            const float input = buffer.samples[index];
            const float low = low_prev[ch] + low_alpha * (input - low_prev[ch]);
            const float high = high_alpha * (high_prev[ch] + input - input_prev[ch]);
            low_prev[ch] = low;
            high_prev[ch] = high;
            input_prev[ch] = input;

            switch (filter_type) {
            case WINGUI_AUDIO_FILTER_LOW_PASS:
                buffer.samples[index] = low;
                break;
            case WINGUI_AUDIO_FILTER_HIGH_PASS:
                buffer.samples[index] = high;
                break;
            case WINGUI_AUDIO_FILTER_BAND_PASS:
                buffer.samples[index] = 0.5f * (low + high);
                break;
            default:
                break;
            }
        }
    }
}

std::unique_ptr<SynthAudioBuffer> createOscillatorBuffer(
    float frequency,
    float duration,
    WaveformType waveform,
    float attack,
    float decay,
    float sustain,
    float release) {
    SynthSoundEffect effect;
    effect.duration = duration;
    Oscillator osc;
    osc.waveform = waveform;
    osc.frequency = std::max(1.0f, frequency);
    osc.amplitude = 0.75f;
    if (waveform == WaveformType::PULSE) {
        osc.pulseWidth = 0.25f;
    }
    effect.oscillators.push_back(osc);
    effect.envelope.attackTime = std::max(0.0f, attack);
    effect.envelope.decayTime = std::max(0.0f, decay);
    effect.envelope.sustainLevel = clamp01(sustain);
    effect.envelope.releaseTime = std::max(0.0f, release);
    return g_audio.synth.generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> createNoiseBuffer(int noise_type, float duration) {
    auto buffer = std::make_unique<SynthAudioBuffer>(kSampleRate, kChannels);
    buffer->resize(std::max(0.01f, duration));

    uint32_t seed = 0x12345678u + static_cast<uint32_t>(noise_type * 977u);
    auto nextWhite = [&seed]() -> float {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<float>((seed >> 8) & 0x00ffffffu) / 8388607.5f) - 1.0f;
    };

    float pink0 = 0.0f;
    float pink1 = 0.0f;
    float pink2 = 0.0f;
    float brown = 0.0f;
    const size_t frames = buffer->getFrameCount();
    for (size_t frame = 0; frame < frames; ++frame) {
        float value = nextWhite();
        if (noise_type == 1) {
            pink0 = 0.99765f * pink0 + value * 0.0990460f;
            pink1 = 0.96300f * pink1 + value * 0.2965164f;
            pink2 = 0.57000f * pink2 + value * 1.0526913f;
            value = (pink0 + pink1 + pink2 + value * 0.1848f) * 0.25f;
        } else if (noise_type == 2) {
            brown += value * 0.02f;
            brown = std::max(-1.0f, std::min(1.0f, brown));
            value = brown;
        }

        const float t = static_cast<float>(frame) / static_cast<float>(frames);
        const float env = std::pow(1.0f - t, 2.0f);
        value *= env * 0.6f;
        for (uint32_t ch = 0; ch < buffer->channels; ++ch) {
            buffer->samples[frame * buffer->channels + ch] = value;
        }
    }

    normalizeBuffer(*buffer);
    return buffer;
}

std::unique_ptr<SynthAudioBuffer> createFMBuffer(float carrier_freq, float mod_freq, float mod_index, float duration) {
    auto buffer = std::make_unique<SynthAudioBuffer>(kSampleRate, kChannels);
    buffer->resize(std::max(0.01f, duration));

    const float carrier = std::max(1.0f, carrier_freq);
    const float modulator = std::max(1.0f, mod_freq);
    const float index = std::max(0.0f, mod_index);
    const size_t frames = buffer->getFrameCount();
    for (size_t frame = 0; frame < frames; ++frame) {
        const float time = static_cast<float>(frame) / static_cast<float>(buffer->sampleRate);
        const float env_t = static_cast<float>(frame) / static_cast<float>(frames);
        const float env = std::pow(1.0f - env_t, 1.5f);
        const float mod = std::sin(2.0f * 3.14159265358979323846f * modulator * time) * index;
        const float sample = std::sin(2.0f * 3.14159265358979323846f * carrier * time + mod) * env * 0.7f;
        for (uint32_t ch = 0; ch < buffer->channels; ++ch) {
            buffer->samples[frame * buffer->channels + ch] = sample;
        }
    }

    normalizeBuffer(*buffer);
    return buffer;
}

bool writeWavFile(const SynthAudioBuffer& buffer, const char* filename, float volume) {
    if (!filename || !*filename) {
        return false;
    }

    FILE* file = nullptr;
    if (fopen_s(&file, filename, "wb") != 0 || !file) {
        return false;
    }

    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = static_cast<uint16_t>(buffer.channels * (bits_per_sample / 8));
    const uint32_t byte_rate = buffer.sampleRate * block_align;
    const uint32_t data_size = static_cast<uint32_t>(buffer.samples.size() * sizeof(int16_t));
    const uint32_t riff_size = 36u + data_size;

    auto write_u16 = [file](uint16_t value) { fwrite(&value, sizeof(value), 1, file); };
    auto write_u32 = [file](uint32_t value) { fwrite(&value, sizeof(value), 1, file); };

    fwrite("RIFF", 1, 4, file);
    write_u32(riff_size);
    fwrite("WAVE", 1, 4, file);
    fwrite("fmt ", 1, 4, file);
    write_u32(16);
    write_u16(1);
    write_u16(static_cast<uint16_t>(buffer.channels));
    write_u32(buffer.sampleRate);
    write_u32(byte_rate);
    write_u16(block_align);
    write_u16(bits_per_sample);
    fwrite("data", 1, 4, file);
    write_u32(data_size);

    const float gain = std::max(0.0f, volume);
    for (float sample : buffer.samples) {
        const float scaled = std::max(-1.0f, std::min(1.0f, sample * gain));
        const int16_t pcm = static_cast<int16_t>(std::lround(scaled * 32767.0f));
        fwrite(&pcm, sizeof(pcm), 1, file);
    }

    fclose(file);
    return true;
}

void fillMixerBlockLocked(MixerBlock& block) {
    std::fill(block.samples.begin(), block.samples.end(), 0);

    std::vector<float> mixed(block.samples.size(), 0.0f);
    size_t voice_index = 0;
    while (voice_index < g_audio.voices.size()) {
        ActiveVoice& voice = g_audio.voices[voice_index];
        const SynthAudioBuffer* buffer = g_audio.bank.getSound(voice.sound_id);
        if (!buffer || buffer->channels == 0) {
            g_audio.voices.erase(g_audio.voices.begin() + static_cast<std::ptrdiff_t>(voice_index));
            continue;
        }

        const size_t total_frames = buffer->getFrameCount();
        size_t write_frame = 0;
        while (write_frame < kFramesPerBlock && voice.frame_pos < total_frames) {
            const size_t src = voice.frame_pos * buffer->channels;
            const float left = buffer->samples[src];
            const float right = buffer->channels > 1 ? buffer->samples[src + 1] : left;
            mixed[write_frame * 2] += left * voice.gain_left * g_audio.master_volume;
            mixed[write_frame * 2 + 1] += right * voice.gain_right * g_audio.master_volume;
            ++write_frame;
            ++voice.frame_pos;
        }

        if (voice.frame_pos >= total_frames) {
            g_audio.voices.erase(g_audio.voices.begin() + static_cast<std::ptrdiff_t>(voice_index));
            continue;
        }
        ++voice_index;
    }

    for (size_t i = 0; i < mixed.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, mixed[i]));
        block.samples[i] = static_cast<int16_t>(std::lround(clamped * 32767.0f));
    }
}

void mixerThreadMain() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_audio.mutex);
            if (g_audio.shutdown_requested) {
                break;
            }

            for (MixerBlock& block : g_audio.blocks) {
                if ((block.header.dwFlags & WHDR_INQUEUE) != 0) {
                    continue;
                }
                fillMixerBlockLocked(block);
                waveOutWrite(g_audio.wave_out, &block.header, sizeof(WAVEHDR));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

bool ensureAudioInitializedLocked() {
    if (g_audio.audio_initialized) {
        return true;
    }

    SynthConfig config;
    config.sampleRate = kSampleRate;
    config.channels = kChannels;
    config.bitDepth = 16;
    config.maxDuration = 10.0f;
    if (!g_audio.synth.initialize(config)) {
        wingui_set_last_error_string_internal("wingui_audio_init: SynthEngine initialization failed");
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(kChannels);
    format.nSamplesPerSec = kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>((format.nChannels * format.wBitsPerSample) / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    if (waveOutOpen(&g_audio.wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_audio.wave_out = nullptr;
        g_audio.synth.shutdown();
        wingui_set_last_error_string_internal("wingui_audio_init: waveOutOpen failed");
        return false;
    }

    g_audio.blocks.clear();
    g_audio.blocks.resize(kMixerBlocks);
    for (MixerBlock& block : g_audio.blocks) {
        block.samples.resize(kFramesPerBlock * kChannels);
        std::memset(&block.header, 0, sizeof(block.header));
        block.header.lpData = reinterpret_cast<LPSTR>(block.samples.data());
        block.header.dwBufferLength = static_cast<DWORD>(block.samples.size() * sizeof(int16_t));
        if (waveOutPrepareHeader(g_audio.wave_out, &block.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            wingui_set_last_error_string_internal("wingui_audio_init: waveOutPrepareHeader failed");
            return false;
        }
    }

    g_audio.shutdown_requested = false;
    g_audio.worker = std::thread(mixerThreadMain);
    g_audio.audio_initialized = true;
    wingui_clear_last_error_internal();
    return true;
}

void shutdownAudioLocked() {
    if (!g_audio.audio_initialized) {
        return;
    }

    g_audio.shutdown_requested = true;
    std::thread worker = std::move(g_audio.worker);
    g_audio.mutex.unlock();
    if (worker.joinable()) {
        worker.join();
    }
    g_audio.mutex.lock();

    if (g_audio.wave_out) {
        waveOutReset(g_audio.wave_out);
        for (MixerBlock& block : g_audio.blocks) {
            waveOutUnprepareHeader(g_audio.wave_out, &block.header, sizeof(WAVEHDR));
        }
        waveOutClose(g_audio.wave_out);
        g_audio.wave_out = nullptr;
    }

    g_audio.blocks.clear();
    g_audio.voices.clear();
    g_audio.bank.freeAll();
    g_audio.synth.shutdown();
    g_audio.audio_initialized = false;
}

bool ensureMidiInitializedLocked() {
    if (g_audio.midi_initialized) {
        return true;
    }

    HMIDIOUT handle = nullptr;
    if (midiOutOpen(&handle, MIDI_MAPPER, 0, 0, 0) != MMSYSERR_NOERROR) {
        wingui_set_last_error_string_internal("wingui_midi_init: midiOutOpen failed");
        return false;
    }

    g_audio.midi_out = handle;
    g_audio.midi_initialized = true;
    wingui_clear_last_error_internal();
    return true;
}

void shutdownMidiLocked() {
    if (!g_audio.midi_initialized) {
        return;
    }
    if (g_audio.midi_out) {
        midiOutReset(g_audio.midi_out);
        midiOutClose(g_audio.midi_out);
        g_audio.midi_out = nullptr;
    }
    g_audio.midi_initialized = false;
}

uint32_t registerGeneratedSoundLocked(std::unique_ptr<SynthAudioBuffer> buffer) {
    if (!buffer) {
        wingui_set_last_error_string_internal("wingui_audio: sound generation failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return g_audio.bank.registerSound(std::move(buffer));
}

uint32_t createAndRegisterPredefined(std::unique_ptr<SynthAudioBuffer> (SynthEngine::*fn)(float, float), float a, float b) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureAudioInitializedLocked()) {
        return 0;
    }
    return registerGeneratedSoundLocked((g_audio.synth.*fn)(a, b));
}

uint32_t createToneSound(float frequency, float duration, int waveform, float attack, float decay, float sustain, float release) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureAudioInitializedLocked()) {
        return 0;
    }
    return registerGeneratedSoundLocked(createOscillatorBuffer(
        frequency,
        duration,
        waveformTypeFromCode(waveform),
        attack,
        decay,
        sustain,
        release));
}

uint32_t createFilteredToneSound(float frequency, float duration, int waveform, int filter_type, float cutoff, float resonance) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureAudioInitializedLocked()) {
        return 0;
    }
    auto buffer = createOscillatorBuffer(frequency, duration, waveformTypeFromCode(waveform), 0.01f, 0.05f, 0.8f, 0.1f);
    if (!buffer) {
        wingui_set_last_error_string_internal("wingui_audio_create_filtered_tone: sound generation failed");
        return 0;
    }
    applySimpleFilter(*buffer, filter_type, cutoff, resonance);
    normalizeBuffer(*buffer);
    return registerGeneratedSoundLocked(std::move(buffer));
}

uint32_t packMidiShortMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    return static_cast<uint32_t>(status) |
        (static_cast<uint32_t>(data1) << 8u) |
        (static_cast<uint32_t>(data2) << 16u);
}

} // namespace

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_init(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return ensureAudioInitializedLocked() ? 1 : 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_audio_shutdown(void) {
    std::unique_lock<std::mutex> lock(g_audio.mutex);
    shutdownAudioLocked();
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_is_initialized(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return g_audio.audio_initialized ? 1 : 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_audio_stop_all(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.voices.clear();
    if (g_audio.wave_out) {
        waveOutReset(g_audio.wave_out);
    }
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_beep(float frequency, float duration) {
    return createAndRegisterPredefined(&SynthEngine::generateBeep, frequency, duration);
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_zap(float frequency, float duration) {
    return createAndRegisterPredefined(&SynthEngine::generateZap, frequency, duration);
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_tone(float frequency, float duration, int32_t waveform) {
    return createToneSound(frequency, duration, waveform, 0.01f, 0.05f, 0.8f, 0.1f);
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_note(
    float midi_note,
    float duration,
    int32_t waveform,
    float attack,
    float decay,
    float sustain,
    float release) {
    const float frequency = SynthEngine::noteToFrequency(static_cast<int>(std::lround(midi_note)));
    return createToneSound(frequency, duration, waveform, attack, decay, sustain, release);
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_noise(int32_t noise_type, float duration) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureAudioInitializedLocked()) {
        return 0;
    }
    return registerGeneratedSoundLocked(createNoiseBuffer(noise_type, duration));
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_fm(float carrier, float modulator, float index, float duration) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureAudioInitializedLocked()) {
        return 0;
    }
    return registerGeneratedSoundLocked(createFMBuffer(carrier, modulator, index, duration));
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_filtered_tone(
    float frequency,
    float duration,
    int32_t waveform,
    int32_t filter_type,
    float cutoff,
    float resonance) {
    return createFilteredToneSound(frequency, duration, waveform, filter_type, cutoff, resonance);
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_filtered_note(
    float midi_note,
    float duration,
    int32_t waveform,
    float attack,
    float decay,
    float sustain,
    float release,
    int32_t filter_type,
    float cutoff,
    float resonance) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureAudioInitializedLocked()) {
        return 0;
    }
    auto buffer = createOscillatorBuffer(
        SynthEngine::noteToFrequency(static_cast<int>(std::lround(midi_note))),
        duration,
        waveformTypeFromCode(waveform),
        attack,
        decay,
        sustain,
        release);
    if (!buffer) {
        wingui_set_last_error_string_internal("wingui_audio_create_filtered_note: sound generation failed");
        return 0;
    }
    applySimpleFilter(*buffer, filter_type, cutoff, resonance);
    normalizeBuffer(*buffer);
    return registerGeneratedSoundLocked(std::move(buffer));
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_play(uint32_t sound_id, float volume, float pan) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureAudioInitializedLocked()) {
        return 0;
    }
    if (!g_audio.bank.hasSound(sound_id)) {
        wingui_set_last_error_string_internal("wingui_audio_play: sound id was not found");
        return 0;
    }

    const float vol = std::max(0.0f, volume);
    const float p = clamp11(pan);
    ActiveVoice voice;
    voice.sound_id = sound_id;
    voice.frame_pos = 0;
    voice.gain_left = vol * (p <= 0.0f ? 1.0f : 1.0f - p);
    voice.gain_right = vol * (p >= 0.0f ? 1.0f : 1.0f + p);
    g_audio.voices.push_back(voice);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_play_simple(uint32_t sound_id) {
    return wingui_audio_play(sound_id, 1.0f, 0.0f);
}

extern "C" WINGUI_API void WINGUI_CALL wingui_audio_stop_sound(uint32_t sound_id) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.voices.erase(
        std::remove_if(g_audio.voices.begin(), g_audio.voices.end(),
            [sound_id](const ActiveVoice& voice) { return voice.sound_id == sound_id; }),
        g_audio.voices.end());
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_is_sound_playing(uint32_t sound_id) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    for (const ActiveVoice& voice : g_audio.voices) {
        if (voice.sound_id == sound_id) {
            return 1;
        }
    }
    return 0;
}

extern "C" WINGUI_API float WINGUI_CALL wingui_audio_sound_duration(uint32_t sound_id) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    const SynthAudioBuffer* buffer = g_audio.bank.getSound(sound_id);
    return buffer ? buffer->duration : 0.0f;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_free_sound(uint32_t sound_id) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.voices.erase(
        std::remove_if(g_audio.voices.begin(), g_audio.voices.end(),
            [sound_id](const ActiveVoice& voice) { return voice.sound_id == sound_id; }),
        g_audio.voices.end());
    const bool freed = g_audio.bank.freeSound(sound_id);
    if (!freed) {
        wingui_set_last_error_string_internal("wingui_audio_free_sound: sound id was not found");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_audio_free_all(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.voices.clear();
    g_audio.bank.freeAll();
    if (g_audio.wave_out) {
        waveOutReset(g_audio.wave_out);
    }
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API void WINGUI_CALL wingui_audio_set_master_volume(float volume) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.master_volume = clamp01(volume);
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API float WINGUI_CALL wingui_audio_get_master_volume(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return g_audio.master_volume;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_sound_exists(uint32_t sound_id) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return g_audio.bank.hasSound(sound_id) ? 1 : 0;
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_audio_sound_count(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return static_cast<uint32_t>(g_audio.bank.getSoundCount());
}

extern "C" WINGUI_API uint64_t WINGUI_CALL wingui_audio_sound_memory_usage(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return static_cast<uint64_t>(g_audio.bank.getMemoryUsage());
}

extern "C" WINGUI_API float WINGUI_CALL wingui_audio_note_to_frequency(int32_t midi_note) {
    return SynthEngine::noteToFrequency(midi_note);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_frequency_to_note(float frequency) {
    return SynthEngine::frequencyToNote(frequency);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_audio_export_wav_utf8(uint32_t sound_id, const char* path_utf8, float volume) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    const SynthAudioBuffer* buffer = g_audio.bank.getSound(sound_id);
    if (!buffer) {
        wingui_set_last_error_string_internal("wingui_audio_export_wav_utf8: sound id was not found");
        return 0;
    }
    if (!writeWavFile(*buffer, path_utf8, volume)) {
        wingui_set_last_error_string_internal("wingui_audio_export_wav_utf8: WAV export failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_midi_init(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return ensureMidiInitializedLocked() ? 1 : 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_midi_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    shutdownMidiLocked();
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_midi_is_initialized(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return g_audio.midi_initialized ? 1 : 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_midi_reset(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (g_audio.midi_out) {
        midiOutReset(g_audio.midi_out);
    }
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_midi_short_message(uint32_t message) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!ensureMidiInitializedLocked()) {
        return 0;
    }
    if (midiOutShortMsg(g_audio.midi_out, message) != MMSYSERR_NOERROR) {
        wingui_set_last_error_string_internal("wingui_midi_short_message: midiOutShortMsg failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_midi_program_change(uint8_t channel, uint8_t program) {
    return wingui_midi_short_message(packMidiShortMessage(static_cast<uint8_t>(0xC0u | (channel & 0x0Fu)), program, 0));
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value) {
    return wingui_midi_short_message(packMidiShortMessage(static_cast<uint8_t>(0xB0u | (channel & 0x0Fu)), controller, value));
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    return wingui_midi_short_message(packMidiShortMessage(static_cast<uint8_t>(0x90u | (channel & 0x0Fu)), note, velocity));
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_midi_note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
    return wingui_midi_short_message(packMidiShortMessage(static_cast<uint8_t>(0x80u | (channel & 0x0Fu)), note, velocity));
}