#pragma once

#include "AudioPlayer.h"
#include "SDL.h"

namespace bochan {
    class BochanAudioPlayer sealed : public AudioPlayer {
    public:
        BochanAudioPlayer() = default;
        BOCHANAPI ~BochanAudioPlayer();
        BochanAudioPlayer(BochanAudioPlayer&) = delete;
        BochanAudioPlayer(BochanAudioPlayer&&) = delete;
        BochanAudioPlayer& operator=(BochanAudioPlayer&) = delete;
        BochanAudioPlayer& operator=(BochanAudioPlayer&&) = delete;
        BOCHANAPI bool initialize(int sampleRate, size_t minBufferSize, size_t maxBufferSize) override;
        BOCHANAPI void deinitialize() override;
        BOCHANAPI bool isInitialized() const override;
        BOCHANAPI size_t queueData(ByteBuffer* buff) override;
        BOCHANAPI bool isPlaying() override;
        BOCHANAPI bool play() override;
        BOCHANAPI void stop() override;
        BOCHANAPI void flush() override;
    private:
        BOCHANAPI static void fillData(void* ptr, Uint8* stream, int len);
        bool initialized{ false }, playing{ false };
        uint8_t* sampleBuffer{ nullptr };
        size_t sampleBufferPos{ 0ULL };
        std::mutex bufferMutex{};
    };
}
