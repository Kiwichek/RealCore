#pragma once
#include <string>
#include <cstdint>

struct ma_engine;
struct ma_sound;

class AudioEngine {
public:
    AudioEngine() = default;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init();
    void shutdown();

    uint64_t playSound(const std::string& path, float volume = 1.0f);
    void stopSound(uint64_t soundId);
    void stopAll();
    void setMasterVolume(float volume);
    float masterVolume() const;

private:
    ma_engine* m_engine = nullptr;
};

struct SoundHandle {
    uint64_t id = 0;
    ma_sound* sound = nullptr;
};
