#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace UMR {
  class Muxer {
  public:
    static Muxer* create(const char* filename);

    ~Muxer();

    bool begin();
    bool mux(AVPacket* packet);
    bool end();
    bool audio();
    bool video();
    bool global_header();
    bool add_stream(AVCodecContext* codec_context);

  private:
    AVFormatContext* m_format_context = nullptr;

    Muxer();
    Muxer(Muxer&& other) noexcept;
  };
}