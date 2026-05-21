#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
}

#include <cstdint>

namespace UMR {
  class Encoder {
  public:
    static Encoder* create(
      int width,
      int height,
      AVCodecID codec_id = AVCodecID::AV_CODEC_ID_MPEG4,
      int64_t bit_rate = 400000,
      int gop_size = 10,
      int max_b_frames = 1
    );

    ~Encoder();

    bool begin(const char* filename);
    bool encode(uint8_t* data, int64_t pts);
    bool end();

  private:
    AVCodecContext* m_codec_context;
    AVPacket* m_packet;
    AVFrame* m_rgba_frame;
    AVFrame* m_yuv420p_frame;
    SwsContext* m_sws_context;
    AVFormatContext* m_format_context = nullptr;

    Encoder(
      AVCodecContext* codec_context,
      AVPacket* packet,
      AVFrame* rgba_frame,
      AVFrame* yuv420p_frame,
      SwsContext* sws_context
    );

    bool encode(AVFrame* frame);
  };
}