#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
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
      int64_t video_bit_rate
    );

    ~Encoder();

    bool encode(uint8_t* data, int64_t pts);
    bool end();

  private:
    AVFormatContext* m_format_context = nullptr;
    AVCodecContext* m_video_codec_context = nullptr;
    AVFrame* m_rgba_frame = nullptr;
    AVFrame* m_yuv420p_frame = nullptr;
    SwsContext* m_sws_context = nullptr;
    AVPacket* m_packet = nullptr;

    Encoder();
    Encoder(Encoder&& other) noexcept;

    bool set_up_video(
      AVCodecID codec_id,
      int width,
      int height,
      int64_t bit_rate
    );
    bool set_up_audio();
    bool encode(AVFrame* frame);
  };
}