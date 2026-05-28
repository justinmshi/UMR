#include <utility>
#include <cstring>

#include "Encoder.hpp"

constexpr int DEFAULT_AUDIO_FRAME_SIZE = 10000;

UMR::Encoder* UMR::Encoder::begin(
  const char* filename,
  AVCodecID video_codec_id,
  int width,
  int height,
  int64_t video_bit_rate,
  AVCodecID audio_codec_id,
  int sample_rate,
  int64_t audio_bit_rate,
  int channels
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

  if (encoder.m_format_context->oformat->video_codec != AVCodecID::AV_CODEC_ID_NONE) {
    if (!encoder.set_up_video(video_codec_id, width, height, video_bit_rate)) {
      return nullptr;
    }
  }

  if (encoder.m_format_context->oformat->audio_codec != AVCodecID::AV_CODEC_ID_NONE) {
    if (!encoder.set_up_audio(audio_codec_id, sample_rate, audio_bit_rate, channels)) {
      return nullptr;
    }
  }

  encoder.m_packet = av_packet_alloc();
  if (!encoder.m_packet) {
    return nullptr;
  }

  if (avformat_write_header(encoder.m_format_context, nullptr) < 0) {
    return nullptr;
  }

  return new Encoder(std::move(encoder));
}

UMR::Encoder::~Encoder() {
  if (m_packet) {
    av_packet_free(&m_packet);
  }
  if (m_audio_frame) {
    av_frame_free(&m_audio_frame);
  }
  if (m_audio_codec_context) {
    avcodec_free_context(&m_audio_codec_context);
  }
  sws_freeContext(m_sws_context);
  if (m_yuv420p_frame) {
    av_frame_free(&m_yuv420p_frame);
  }
  if (m_rgba_frame) {
    av_frame_free(&m_rgba_frame);
  }
  if (m_video_codec_context) {
    avcodec_free_context(&m_video_codec_context);
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
  m_video_codec_context(other.m_video_codec_context),
  m_rgba_frame(other.m_rgba_frame),
  m_yuv420p_frame(other.m_yuv420p_frame),
  m_sws_context(other.m_sws_context),
  m_audio_codec_context(other.m_audio_codec_context),
  m_audio_frame(other.m_audio_frame),
  m_packet(other.m_packet) {
  other.m_format_context = nullptr;
  other.m_video_codec_context = nullptr;
  other.m_rgba_frame = nullptr;
  other.m_yuv420p_frame = nullptr;
  other.m_sws_context = nullptr;
  other.m_audio_codec_context = nullptr;
  other.m_audio_frame = nullptr;
  other.m_packet = nullptr;
}

bool UMR::Encoder::set_up_video(
  AVCodecID codec_id,
  int width,
  int height,
  int64_t bit_rate
  // int gop_size
  // int max_b_frames
) {
  if (!m_format_context) {
    return false;
  }

  const AVCodec* codec = avcodec_find_encoder(codec_id);
  if (!codec) {
    return false;
  }

  m_video_codec_context = avcodec_alloc_context3(codec);
  if (!m_video_codec_context) {
    return false;
  }
  m_video_codec_context->width = width;
  m_video_codec_context->height = height;
  m_video_codec_context->bit_rate = bit_rate;
  m_video_codec_context->time_base = {.num = 1, .den = 1000};
  // m_codec_context->gop_size = gop_size;
  // m_codec_context->max_b_frames = max_b_frames;
  m_video_codec_context->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
  if (m_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
    m_video_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  if (avcodec_open2(m_video_codec_context, codec, nullptr) < 0) {
    return false;
  }

  m_rgba_frame = av_frame_alloc();
  if (!m_rgba_frame) {
    return false;
  }
  m_rgba_frame->format = AVPixelFormat::AV_PIX_FMT_RGBA;
  m_rgba_frame->width = m_video_codec_context->width;
  m_rgba_frame->height = m_video_codec_context->height;
  if (av_frame_get_buffer(m_rgba_frame, 0) < 0) {
    return false;
  }

  m_yuv420p_frame = av_frame_alloc();
  if (!m_yuv420p_frame) {
    return false;
  }

  // TODO: handle variable width/height?
  m_sws_context = sws_getContext(
    m_video_codec_context->width,
    m_video_codec_context->height,
    static_cast<AVPixelFormat>(m_rgba_frame->format),
    m_video_codec_context->width,
    m_video_codec_context->height,
    m_video_codec_context->pix_fmt,
    SWS_BICUBIC,
    nullptr,
    nullptr,
    nullptr
  );
  if (!m_sws_context) {
    return false;
  }

  AVStream* stream = avformat_new_stream(m_format_context, nullptr);
  if (!stream) {
    return false;
  }
  stream->id = m_format_context->nb_streams - 1;
  stream->time_base = m_video_codec_context->time_base;
  if (avcodec_parameters_from_context(stream->codecpar, m_video_codec_context) < 0) {
    return false;
  }

  return true;
}

bool UMR::Encoder::set_up_audio(AVCodecID codec_id, int sample_rate, int64_t bit_rate, int channels) {
  if (!m_format_context) {
    return false;
  }

  const AVCodec* codec = avcodec_find_encoder(codec_id);
  if (!codec) {
    return false;
  }

  m_audio_codec_context = avcodec_alloc_context3(codec);
  if (!m_audio_codec_context) {
    return false;
  }
  m_audio_codec_context->sample_rate = sample_rate;
  m_audio_codec_context->bit_rate = bit_rate;
  m_audio_codec_context->time_base = {.num = 1, .den = m_audio_codec_context->sample_rate};
  m_audio_codec_context->sample_fmt = AVSampleFormat::AV_SAMPLE_FMT_FLTP;
  AVChannelLayout channel_layout;
  switch (channels) {
    case 1:
    {
      channel_layout = AV_CHANNEL_LAYOUT_MONO;
      break;
    }
    case 2:
    {
      channel_layout = AV_CHANNEL_LAYOUT_STEREO;
      break;
    }
    case 4:
    {
      channel_layout = AV_CHANNEL_LAYOUT_QUAD;
      break;
    }
    case 5:
    {
      channel_layout = AV_CHANNEL_LAYOUT_5POINT0;
      break;
    }
    case 6:
    {
      channel_layout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
      break;
    }
    case 8:
    {
      channel_layout = AV_CHANNEL_LAYOUT_7POINT1;
      break;
    }
    default:
    {
      return false;
    }
  }
  if (av_channel_layout_copy(&m_audio_codec_context->ch_layout, &channel_layout) < 0) {
    return false;
  }
  if (m_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
    m_audio_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  if (avcodec_open2(m_audio_codec_context, codec, nullptr) < 0) {
    return false;
  }

  // TODO: resampling?
  m_audio_frame = av_frame_alloc();
  if (!m_audio_frame) {
    return false;
  }
  m_audio_frame->format = m_audio_codec_context->sample_fmt;
  if (m_audio_codec_context->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
    m_audio_frame->nb_samples = DEFAULT_AUDIO_FRAME_SIZE;
  } else {
    m_audio_frame->nb_samples = m_audio_codec_context->frame_size;
  }
  if (av_channel_layout_copy(&m_audio_frame->ch_layout, &m_audio_codec_context->ch_layout) < 0) {
    return false;
  }
  if (av_frame_get_buffer(m_audio_frame, 0) < 0) {
    return false;
  }

  AVStream* stream = avformat_new_stream(m_format_context, nullptr);
  if (!stream) {
    return false;
  }
  stream->id = m_format_context->nb_streams - 1;
  stream->time_base = m_audio_codec_context->time_base;
  if (avcodec_parameters_from_context(stream->codecpar, m_audio_codec_context) < 0) {
    return false;
  }

  return true;
}

bool UMR::Encoder::encode(AVFrame* frame) {
  int stream_index = av_find_best_stream(m_format_context, AVMediaType::AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    return false;
  }
  AVStream* stream = m_format_context->streams[stream_index];

  if (avcodec_send_frame(m_video_codec_context, frame) != 0) {
    return false;
  }
  while (avcodec_receive_packet(m_video_codec_context, m_packet) == 0) {
    av_packet_rescale_ts(m_packet, m_video_codec_context->time_base, stream->time_base);
    m_packet->stream_index = stream->index;
    if (av_interleaved_write_frame(m_format_context, m_packet) < 0) {
      return false;
    }
  }

  return true;
}