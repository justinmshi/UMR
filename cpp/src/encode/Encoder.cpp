#include <utility>
#include <cstring>

#include "Encoder.hpp"

UMR::Encoder* UMR::Encoder::begin(
  const char* filename,
  AVCodecID codec_id,
  int width,
  int height,
  int64_t bit_rate
  // int gop_size
  // int max_b_frames
) {
  Encoder encoder;

  if (avformat_alloc_output_context2(&encoder.m_format_context, nullptr, nullptr, filename) < 0) {
    return nullptr;
  }
  if (!(encoder.m_format_context->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&encoder.m_format_context->pb, filename, AVIO_FLAG_WRITE) < 0) {
      return nullptr;
    }
  }

  const AVCodec* codec = avcodec_find_encoder(codec_id);
  if (!codec) {
    return nullptr;
  }

  encoder.m_codec_context = avcodec_alloc_context3(codec);
  if (!encoder.m_codec_context) {
    return nullptr;
  }
  encoder.m_codec_context->width = width;
  encoder.m_codec_context->height = height;
  encoder.m_codec_context->bit_rate = bit_rate;
  encoder.m_codec_context->time_base = {.num = 1, .den = 1000};
  // encoder.m_codec_context->gop_size = gop_size;
  // encoder.m_codec_context->max_b_frames = max_b_frames;
  encoder.m_codec_context->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
  if (encoder.m_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
    encoder.m_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  if (avcodec_open2(encoder.m_codec_context, codec, nullptr) < 0) {
    return nullptr;
  }

  encoder.m_packet = av_packet_alloc();
  if (!encoder.m_packet) {
    return nullptr;
  }

  encoder.m_rgba_frame = av_frame_alloc();
  if (!encoder.m_rgba_frame) {
    return nullptr;
  }
  encoder.m_rgba_frame->format = AVPixelFormat::AV_PIX_FMT_RGBA;
  encoder.m_rgba_frame->width = width;
  encoder.m_rgba_frame->height = height;
  if (av_frame_get_buffer(encoder.m_rgba_frame, 0) < 0) {
    return nullptr;
  }

  encoder.m_yuv420p_frame = av_frame_alloc();
  if (!encoder.m_yuv420p_frame) {
    return nullptr;
  }

  encoder.m_sws_context = sws_getContext(
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
  if (!encoder.m_sws_context) {
    return nullptr;
  }

  AVStream* stream = avformat_new_stream(encoder.m_format_context, nullptr);
  if (!stream) {
    return nullptr;
  }
  stream->id = encoder.m_format_context->nb_streams - 1;
  stream->time_base = encoder.m_codec_context->time_base;
  if (avcodec_parameters_from_context(stream->codecpar, encoder.m_codec_context) < 0) {
    return nullptr;
  }

  if (avformat_write_header(encoder.m_format_context, nullptr) < 0) {
    return nullptr;
  }

  return new Encoder(std::move(encoder));
}

UMR::Encoder::~Encoder() {
  sws_freeContext(m_sws_context);
  if (m_yuv420p_frame) {
    av_frame_free(&m_yuv420p_frame);
  }
  if (m_rgba_frame) {
    av_frame_free(&m_rgba_frame);
  }
  if (m_packet) {
    av_packet_free(&m_packet);
  }
  if (m_codec_context) {
    avcodec_free_context(&m_codec_context);
  }
  if (m_format_context) {
    if (m_format_context->pb) {
      avio_closep(&m_format_context->pb);
    }
    avformat_free_context(m_format_context);
  }
}

bool UMR::Encoder::encode(uint8_t* data, int64_t pts) {
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

  return encode(m_yuv420p_frame);
}

bool UMR::Encoder::end() {
  if (!encode(static_cast<AVFrame*>(nullptr))) {
    return false;
  }

  if (av_write_trailer(m_format_context) != 0) {
    return false;
  }

  return true;
}

UMR::Encoder::Encoder() {}

UMR::Encoder::Encoder(Encoder&& other) noexcept:
  m_format_context(other.m_format_context),
  m_codec_context(other.m_codec_context),
  m_packet(other.m_packet),
  m_rgba_frame(other.m_rgba_frame),
  m_yuv420p_frame(other.m_yuv420p_frame),
  m_sws_context(other.m_sws_context) {
  other.m_format_context = nullptr;
  other.m_codec_context = nullptr;
  other.m_packet = nullptr;
  other.m_rgba_frame = nullptr;
  other.m_yuv420p_frame = nullptr;
  other.m_sws_context = nullptr;
}

bool UMR::Encoder::encode(AVFrame* frame) {
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