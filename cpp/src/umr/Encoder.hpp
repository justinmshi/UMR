#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
}

#include <cstdint>
#include <vector>

#include "Muxer.hpp"

namespace UMR {
  class Encoder {
  public:
    static Encoder* create(
      Muxer* muxer,
      AVCodecID audio_codec_id,
      int sample_rate,
      int64_t audio_bit_rate,
      int channels,
      int audio_buffer_size,
      AVCodecID video_codec_id,
      int width,
      int height,
      int64_t video_bit_rate
    );

    ~Encoder();

    std::vector<AVPacket*>* encode_audio(float* data);
    std::vector<AVPacket*>* encode_video(uint8_t* data, int64_t pts);
    std::vector<AVPacket*>* flush();

  private:
    AVCodecContext* m_audio_codec_context = nullptr;
    AVFrame* m_audio_frame = nullptr;
    SwrContext* m_swr_context = nullptr;
    AVAudioFifo* m_audio_fifo = nullptr;
    int m_audio_buffer_size = 0;
    float** m_audio_buffers = nullptr;
    int64_t m_next_audio_pts = 0;
    AVCodecContext* m_video_codec_context = nullptr;
    AVFrame* m_video_frame = nullptr;
    SwsContext* m_sws_context = nullptr;

    Encoder();
    Encoder(Encoder&& other) noexcept;

    bool initialize_audio(
      Muxer* muxer,
      AVCodecID codec_id,
      int sample_rate,
      int64_t bit_rate,
      int channels,
      int buffer_size
    );
    bool initialize_video(
      Muxer* muxer,
      AVCodecID codec_id,
      int width,
      int height,
      int64_t bit_rate
    );
    bool encode(std::vector<AVPacket*>* packets, AVCodecContext* codec_context, AVFrame* frame);
  };
}