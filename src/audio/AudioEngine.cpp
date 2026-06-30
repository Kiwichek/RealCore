#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <audio/AudioEngine.h>
#include <algorithm>
#include <cstdio>

bool AudioEngine::init() {
    ma_engine* engine = new ma_engine();
    ma_result result = ma_engine_init(NULL, engine);
    if (result != MA_SUCCESS) {
        delete engine;
        return false;
    }
    m_engine = engine;
    return true;
}

void AudioEngine::shutdown() {
    if (m_engine) {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }
}

uint64_t AudioEngine::playSound(const std::string& path, float volume) {
    if (!m_engine) return 0;

    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(m_engine, path.c_str(), 0, NULL, NULL, sound);
    if (result != MA_SUCCESS) {
        delete sound;
        return 0;
    }

    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);

    return reinterpret_cast<uint64_t>(sound);
}

void AudioEngine::stopSound(uint64_t soundId) {
    if (soundId == 0) return;
    ma_sound* sound = reinterpret_cast<ma_sound*>(soundId);
    ma_sound_stop(sound);
    ma_sound_uninit(sound);
    delete sound;
}

void AudioEngine::stopAll() {
    if (m_engine) {
        ma_engine_stop(m_engine);
    }
}

void AudioEngine::setMasterVolume(float volume) {
    if (m_engine) {
        ma_engine_set_volume(m_engine, volume);
    }
}

float AudioEngine::masterVolume() const {
    if (m_engine) {
        return ma_engine_get_volume(m_engine);
    }
    return 0.0f;
}
