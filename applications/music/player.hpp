// Audio playback for the music app: one decoder thread per open file that demuxes, decodes and resamples to
// the output's float format, feeding mayo::AudioOutput a little ahead of the device. Seeks bump a serial so
// stale samples are dropped. The position is the time of the last sample handed over minus what is still
// queued, so it follows what you hear.
#pragma once

#include <mayonnaise/audio.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct AVFormatContext;
struct AVCodecContext;

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    /// Opens a file (paused). Returns false with a human-readable error.
    bool open(const std::string& path, std::string& error);
    void close();
    [[nodiscard]] bool isOpen() const { return m_format != nullptr; }
    [[nodiscard]] bool hasOutput() const { return m_output && m_output->isOpen(); }

    void play();
    void pause();
    [[nodiscard]] bool isPlaying() const { return m_playing; }
    void seek(double seconds);
    [[nodiscard]] double position() const;
    [[nodiscard]] double duration() const { return m_duration; }
    /// Everything was decoded and the device has played it.
    [[nodiscard]] bool ended() const;
    void setVolume(float volume);
    /// "FLAC · 44.1 kHz · 16-bit", for the info popover.
    [[nodiscard]] const std::string& formatDescription() const { return m_description; }

private:
    void decodeLoop();
    void stopThread();

    std::unique_ptr<mayo::AudioOutput> m_output;
    AVFormatContext* m_format = nullptr;
    AVCodecContext* m_codec = nullptr;
    int m_stream = -1;
    double m_duration = 0;
    std::string m_description;

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_eof{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<double> m_seekTarget{0};
    std::atomic<double> m_end{0};  // stream time just past the last sample handed to the device
    mutable std::mutex m_seekMutex;
};
