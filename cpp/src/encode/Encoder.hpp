#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
}

#include <cstdint>

namespace UMR {
  class Encoder {
  public:
    static Encoder* begin(
      const char* filename,
      AVCodecID video_codec_id,
      int width,
      int height,
      int64_t video_bit_rate,
      AVCodecID audio_codec_id,
      int sample_rate,
      int64_t audio_bit_rate,
      int channels,
      int audio_buffer_size
    );

    ~Encoder();

    bool send_video(uint8_t* data, int64_t pts);
    bool send_audio(float* data);
    bool end();

  private:
    AVFormatContext* m_format_context = nullptr;
    AVCodecContext* m_video_codec_context = nullptr;
    AVFrame* m_video_frame = nullptr;
    SwsContext* m_sws_context = nullptr;
    AVCodecContext* m_audio_codec_context = nullptr;
    AVFrame* m_audio_frame = nullptr;
    SwrContext* m_swr_context = nullptr;
    AVAudioFifo* m_audio_fifo = nullptr;
    int m_audio_buffer_size = 0;
    float** m_audio_buffers = nullptr;
    int64_t m_next_audio_pts = 0;
    AVPacket* m_packet = nullptr;

    Encoder();
    Encoder(Encoder&& other) noexcept;

    bool initialize_video(
      AVCodecID codec_id,
      int width,
      int height,
      int64_t bit_rate
    );
    bool initialize_audio(
      AVCodecID codec_id,
      int sample_rate,
      int64_t bit_rate,
      int channels,
      int buffer_size
    );
    bool encode(AVMediaType media_type, AVFrame* frame);
  };
}