#include <cstring>

#include "Encoder.hpp"

UMR::Encoder* UMR::Encoder::create(
  int width,
  int height,
  AVCodecID codec_id,
  int64_t bit_rate,
  int gop_size,
  int max_b_frames
) {
  const AVCodec* codec = avcodec_find_encoder(codec_id);
  if (!codec) {
    return nullptr;
  }

  AVCodecContext* codec_context = avcodec_alloc_context3(codec);
  if (!codec_context) {
    return nullptr;
  }
  codec_context->bit_rate = bit_rate;
  codec_context->width = width;
  codec_context->height = height;
  codec_context->time_base = {.num = 1, .den = 1000};
  codec_context->gop_size = gop_size;
  codec_context->max_b_frames = max_b_frames;
  codec_context->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
  if (avcodec_open2(codec_context, codec, nullptr) < 0) {
    avcodec_free_context(&codec_context);
    return nullptr;
  }

  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    avcodec_free_context(&codec_context);
    return nullptr;
  }

  AVFrame* rgba_frame = av_frame_alloc();
  if (!rgba_frame) {
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    return nullptr;
  }
  rgba_frame->format = AVPixelFormat::AV_PIX_FMT_RGBA;
  rgba_frame->width = width;
  rgba_frame->height = height;
  if (av_frame_get_buffer(rgba_frame, 0) < 0) {
    av_frame_free(&rgba_frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    return nullptr;
  }

  AVFrame* yuv420p_frame = av_frame_alloc();
  if (!yuv420p_frame) {
    av_frame_free(&rgba_frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    return nullptr;
  }

  SwsContext* sws_context = sws_getContext(
    width,
    height,
    AVPixelFormat::AV_PIX_FMT_RGBA,
    width,
    height,
    AVPixelFormat::AV_PIX_FMT_YUV420P,
    SWS_BICUBIC,
    nullptr,
    nullptr,
    nullptr
  );
  if (!sws_context) {
    av_frame_free(&yuv420p_frame);
    av_frame_free(&rgba_frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    return nullptr;
  }

  return new Encoder(codec_context, packet, rgba_frame, yuv420p_frame, sws_context);
}

UMR::Encoder::~Encoder() {
  if (m_format_context) {
    if (m_format_context->pb) {
      avio_closep(&m_format_context->pb);
    }
    avformat_free_context(m_format_context);
  }
  sws_freeContext(m_sws_context);
  av_frame_free(&m_yuv420p_frame);
  av_frame_free(&m_rgba_frame);
  av_packet_free(&m_packet);
  avcodec_free_context(&m_codec_context);
}

bool UMR::Encoder::begin(const char* filename) {
  if (m_format_context) {
    return false;
  }

  if (avformat_alloc_output_context2(&m_format_context, nullptr, nullptr, filename) < 0) {
    return false;
  }
  if (!(m_format_context->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&m_format_context->pb, filename, AVIO_FLAG_WRITE) < 0) {
      return false;
    }
  }

  if (m_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
    m_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  } else {
    m_codec_context->flags &= ~AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  AVStream* stream = avformat_new_stream(m_format_context, nullptr);
  if (!stream) {
    return false;
  }
  stream->id = m_format_context->nb_streams - 1;
  stream->time_base = m_codec_context->time_base;
  if (avcodec_parameters_from_context(stream->codecpar, m_codec_context) < 0) {
    return false;
  }

  if (avformat_write_header(m_format_context, nullptr) < 0) {
    return false;
  }

  return true;
}

bool UMR::Encoder::encode(uint8_t* data, int64_t pts) {
  if (!m_format_context) {
    return false;
  }

  if (av_frame_make_writable(m_rgba_frame) < 0) {
    return false;
  }
  for (int y = 0; y < m_rgba_frame->height; y++) {
    memcpy(m_rgba_frame->data[0] + y * m_rgba_frame->linesize[0], data + y * m_rgba_frame->width * 4, m_rgba_frame->width * 4);
  }
  if (sws_scale_frame(m_sws_context, m_yuv420p_frame, m_rgba_frame) < 0) {
    return false;
  }
  m_yuv420p_frame->pts = pts;

  if (!encode(m_yuv420p_frame)) {
    return false;
  }

  return true;
}

bool UMR::Encoder::end() {
  if (!m_format_context) {
    return false;
  }

  if (!encode(static_cast<AVFrame*>(nullptr))) {
    return false;
  }

  if (av_write_trailer(m_format_context) != 0) {
    return false;
  }

  if (m_format_context->pb) {
    if (avio_closep(&m_format_context->pb) < 0) {
      return false;
    }
  }
  avformat_free_context(m_format_context);
  m_format_context = nullptr;

  return true;
}

UMR::Encoder::Encoder(
  AVCodecContext* codec_context,
  AVPacket* packet,
  AVFrame* rgba_frame,
  AVFrame* yuv420p_frame,
  SwsContext* sws_context
):
  m_codec_context(codec_context),
  m_packet(packet),
  m_rgba_frame(rgba_frame),
  m_yuv420p_frame(yuv420p_frame),
  m_sws_context(sws_context) {
}

bool UMR::Encoder::encode(AVFrame* frame) {
  if (!m_format_context) {
    return false;
  }

  int stream_index = av_find_best_stream(m_format_context, AVMediaType::AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    return false;
  }
  AVStream* stream = m_format_context->streams[stream_index];

  if (avcodec_send_frame(m_codec_context, frame) != 0) {
    return false;
  }
  while (avcodec_receive_packet(m_codec_context, m_packet) == 0) {
    av_packet_rescale_ts(m_packet, m_codec_context->time_base, stream->time_base);
    m_packet->stream_index = stream->index;
    if (av_interleaved_write_frame(m_format_context, m_packet) < 0) {
      return false;
    }
  }

  return true;
}