#include "player.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr int kRate = 48000;
constexpr double kAhead = 0.4;  // seconds queued at the output before the decoder waits

std::string avError(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof buf);
    return buf;
}

}  // namespace

AudioPlayer::AudioPlayer() : m_output(std::make_unique<mayo::AudioOutput>(kRate, 2)) { m_output->pause(); }

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::open(const std::string& path, std::string& error) {
    close();
    if (!hasOutput()) {
        error = "No sound output device is available.";
        return false;
    }
    int r = avformat_open_input(&m_format, path.c_str(), nullptr, nullptr);
    if (r < 0) {
        error = avError(r);
        m_format = nullptr;
        return false;
    }
    if ((r = avformat_find_stream_info(m_format, nullptr)) < 0) {
        error = avError(r);
        close();
        return false;
    }
    const AVCodec* codec = nullptr;
    m_stream = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (m_stream < 0 || !codec) {
        error = "The file has no audio.";
        close();
        return false;
    }
    m_codec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codec, m_format->streams[m_stream]->codecpar);
    if ((r = avcodec_open2(m_codec, codec, nullptr)) < 0) {
        error = avError(r);
        close();
        return false;
    }
    const AVStream* st = m_format->streams[m_stream];
    m_duration = st->duration != AV_NOPTS_VALUE ? static_cast<double>(st->duration) * av_q2d(st->time_base)
                 : m_format->duration > 0       ? static_cast<double>(m_format->duration) / AV_TIME_BASE
                                                : 0;

    std::string name = avcodec_get_name(m_codec->codec_id);
    for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    char buf[96];
    const int bits = m_codec->bits_per_raw_sample > 0 ? m_codec->bits_per_raw_sample : 0;
    if (bits > 0)
        std::snprintf(buf, sizeof buf, "%s · %.1f kHz · %d-bit", name.c_str(), m_codec->sample_rate / 1000.0, bits);
    else if (m_format->bit_rate > 0)
        std::snprintf(buf, sizeof buf, "%s · %.1f kHz · %lld kbps", name.c_str(), m_codec->sample_rate / 1000.0,
                      static_cast<long long>(m_format->bit_rate / 1000));
    else
        std::snprintf(buf, sizeof buf, "%s · %.1f kHz", name.c_str(), m_codec->sample_rate / 1000.0);
    m_description = buf;

    m_stop = false;
    m_eof = false;
    m_seekRequested = false;
    m_end = 0;
    m_output->clear();
    m_thread = std::thread([this] { decodeLoop(); });
    return true;
}

void AudioPlayer::stopThread() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

void AudioPlayer::close() {
    stopThread();
    m_playing = false;
    if (m_output) {
        m_output->pause();
        m_output->clear();
    }
    avcodec_free_context(&m_codec);
    if (m_format) avformat_close_input(&m_format);
    m_stream = -1;
    m_duration = 0;
    m_end = 0;
    m_eof = false;
    m_description.clear();
}

void AudioPlayer::play() {
    if (!isOpen()) return;
    m_playing = true;
    m_output->resume();
}

void AudioPlayer::pause() {
    m_playing = false;
    if (m_output) m_output->pause();
}

void AudioPlayer::seek(double seconds) {
    if (!isOpen()) return;
    std::scoped_lock lock(m_seekMutex);
    m_seekTarget = std::clamp(seconds, 0.0, std::max(0.0, m_duration));
    m_seekRequested = true;
    m_eof = false;
}

double AudioPlayer::position() const {
    if (!isOpen()) return 0;
    if (m_seekRequested) return m_seekTarget;
    return std::clamp(m_end.load() - m_output->queuedSeconds(), 0.0, std::max(m_duration, m_end.load()));
}

bool AudioPlayer::ended() const { return isOpen() && m_eof && !m_seekRequested && m_output->queuedSeconds() < 0.02; }

void AudioPlayer::setVolume(float volume) {
    if (m_output) m_output->setVolume(volume);
}

void AudioPlayer::decodeLoop() {
    AVChannelLayout inLayout{};
    if (m_codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&inLayout, m_codec->ch_layout.nb_channels);
    else av_channel_layout_copy(&inLayout, &m_codec->ch_layout);
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext* swr = nullptr;
    swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, kRate, &inLayout, m_codec->sample_fmt, m_codec->sample_rate, 0,
                        nullptr);
    if (swr && swr_init(swr) < 0) swr_free(&swr);
    av_channel_layout_uninit(&inLayout);

    const double tb = av_q2d(m_format->streams[m_stream]->time_base);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<float> out;
    double skipUntil = -1;  // accurate seeks drop samples before the target
    bool draining = false;

    while (!m_stop && swr) {
        if (m_seekRequested) {
            double target = 0;
            {
                std::scoped_lock lock(m_seekMutex);
                target = m_seekTarget;
                m_seekRequested = false;
            }
            const auto ts = static_cast<int64_t>(target / tb);
            av_seek_frame(m_format, m_stream, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(m_codec);
            swr_init(swr);
            m_output->clear();
            m_end = target;
            m_eof = false;
            draining = false;
            skipUntil = target;
        }
        if (m_eof || m_output->queuedSeconds() > kAhead) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!draining) {
            const int r = av_read_frame(m_format, pkt);
            if (r < 0) {
                draining = true;
                avcodec_send_packet(m_codec, nullptr);  // flush the decoder's last frames
            } else {
                if (pkt->stream_index == m_stream) avcodec_send_packet(m_codec, pkt);
                av_packet_unref(pkt);
            }
        }
        while (!m_stop && !m_seekRequested) {
            const int r = avcodec_receive_frame(m_codec, frame);
            if (r == AVERROR_EOF) {
                m_eof = true;
                break;
            }
            if (r < 0) break;
            const int64_t ts = frame->best_effort_timestamp;
            double pts = ts != AV_NOPTS_VALUE ? static_cast<double>(ts) * tb : m_end.load();
            const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
            out.resize(static_cast<size_t>(std::max(0, maxOut)) * 2);
            auto* dst = reinterpret_cast<uint8_t*>(out.data());
            const int n = swr_convert(swr, &dst, maxOut, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            av_frame_unref(frame);
            if (n <= 0) continue;
            size_t first = 0;
            if (skipUntil >= 0) {
                if (pts + static_cast<double>(n) / kRate <= skipUntil) continue;
                if (pts < skipUntil) first = static_cast<size_t>((skipUntil - pts) * kRate);
                pts = std::max(pts, skipUntil);
                skipUntil = -1;
            }
            if (first < static_cast<size_t>(n)) m_output->write({out.data() + first * 2, (static_cast<size_t>(n) - first) * 2});
            m_end = pts + static_cast<double>(static_cast<size_t>(n) - first) / kRate;
        }
        if (draining && !m_eof && !m_seekRequested) m_eof = true;
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
}
