#include <utility>

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
  int channels,
  int audio_buffer_size
) {
  Encoder encoder;

  if (avformat_alloc_output_context2(&encoder.m_format_context, nullptr, nullptr, filename) < 0) {
    return nullptr;
  }
  if (!(encoder.m_format_context->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&encoder.m_format_context->pb, encoder.m_format_context->url, AVIO_FLAG_WRITE) < 0) {
      return nullptr;
    }
  }

  if (video_codec_id != AVCodecID::AV_CODEC_ID_NONE && encoder.m_format_context->oformat->video_codec != AVCodecID::AV_CODEC_ID_NONE) {
    if (!encoder.initialize_video(video_codec_id, width, height, video_bit_rate)) {
      return nullptr;
    }
  }

  if (audio_codec_id != AVCodecID::AV_CODEC_ID_NONE && encoder.m_format_context->oformat->audio_codec != AVCodecID::AV_CODEC_ID_NONE) {
    if (!encoder.initialize_audio(audio_codec_id, sample_rate, audio_bit_rate, channels, audio_buffer_size)) {
      return nullptr;
    }
  }

  if (!encoder.m_video_codec_context && !encoder.m_audio_codec_context) {
    return nullptr;
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

  if (m_audio_buffers) {
    for (int i = 0; i < m_audio_codec_context->ch_layout.nb_channels; i++) {
      delete m_audio_buffers[i];
    }
    delete m_audio_buffers;
  }

  if (m_audio_fifo) {
    av_audio_fifo_free(m_audio_fifo);
  }

  if (m_swr_context) {
    swr_free(&m_swr_context);
  }

  if (m_audio_frame) {
    av_frame_free(&m_audio_frame);
  }

  if (m_audio_codec_context) {
    avcodec_free_context(&m_audio_codec_context);
  }

  sws_freeContext(m_sws_context);

  if (m_video_frame) {
    av_frame_free(&m_video_frame);
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

bool UMR::Encoder::send_video(uint8_t* data, int64_t pts) {
  if (!m_video_codec_context) {
    return false;
  }

  int stride = m_sws_context->src_w * 4;
  if (sws_scale(
    m_sws_context,
    &data,
    &stride,
    0,
    m_sws_context->src_h,
    m_video_frame->data,
    m_video_frame->linesize
  ) != m_sws_context->dst_h) {
    return false;
  }
  m_video_frame->pts = pts;

  return encode(AVMediaType::AVMEDIA_TYPE_VIDEO, m_video_frame);
}

bool UMR::Encoder::send_audio(float* data) {
  if (!m_audio_codec_context) {
    return false;
  }

  if (swr_convert(
    m_swr_context,
    reinterpret_cast<uint8_t**>(m_audio_buffers),
    m_audio_buffer_size,
    reinterpret_cast<uint8_t**>(&data),
    m_audio_buffer_size
  ) != m_audio_buffer_size) {
    return false;
  }

  if (av_audio_fifo_write(
    m_audio_fifo,
    reinterpret_cast<void**>(m_audio_buffers),
    m_audio_buffer_size
  ) != m_audio_buffer_size) {
    return false;
  }

  if (av_audio_fifo_size(m_audio_fifo) >= m_audio_frame->nb_samples) {
    if (av_audio_fifo_read(
      m_audio_fifo,
      reinterpret_cast<void**>(m_audio_frame->data),
      m_audio_frame->nb_samples
    ) != m_audio_frame->nb_samples) {
      return false;
    }
    m_audio_frame->pts = m_next_audio_pts;
    m_next_audio_pts += m_audio_frame->nb_samples;

    return encode(AVMediaType::AVMEDIA_TYPE_AUDIO, m_audio_frame);
  }

  return true;
}

bool UMR::Encoder::end() {
  if (m_video_codec_context) {
    if (!encode(AVMediaType::AVMEDIA_TYPE_VIDEO, nullptr)) {
      return false;
    }
  }

  if (m_audio_codec_context) {
    int remaining_samples = av_audio_fifo_size(m_audio_fifo);
    if (remaining_samples > 0) {
      if (av_audio_fifo_read(
        m_audio_fifo,
        reinterpret_cast<void**>(m_audio_frame->data),
        remaining_samples
      ) != remaining_samples) {
        return false;
      }
      m_audio_frame->nb_samples = remaining_samples;
      m_audio_frame->pts = m_next_audio_pts;

      if (!encode(AVMediaType::AVMEDIA_TYPE_AUDIO, m_audio_frame)) {
        return false;
      }
    }

    if (!encode(AVMediaType::AVMEDIA_TYPE_AUDIO, nullptr)) {
      return false;
    }
  }

  return av_write_trailer(m_format_context) == 0;
}

UMR::Encoder::Encoder() {}

UMR::Encoder::Encoder(Encoder&& other) noexcept:
  m_format_context(other.m_format_context),
  m_video_codec_context(other.m_video_codec_context),
  m_video_frame(other.m_video_frame),
  m_sws_context(other.m_sws_context),
  m_audio_codec_context(other.m_audio_codec_context),
  m_audio_frame(other.m_audio_frame),
  m_swr_context(other.m_swr_context),
  m_audio_fifo(other.m_audio_fifo),
  m_audio_buffer_size(other.m_audio_buffer_size),
  m_audio_buffers(other.m_audio_buffers),
  m_next_audio_pts(other.m_next_audio_pts),
  m_packet(other.m_packet) {
  other.m_format_context = nullptr;
  other.m_video_codec_context = nullptr;
  other.m_video_frame = nullptr;
  other.m_sws_context = nullptr;
  other.m_audio_codec_context = nullptr;
  other.m_audio_frame = nullptr;
  other.m_swr_context = nullptr;
  other.m_audio_fifo = nullptr;
  other.m_audio_buffer_size = 0;
  other.m_audio_buffers = nullptr;
  other.m_next_audio_pts = 0;
  other.m_packet = nullptr;
}

bool UMR::Encoder::initialize_video(
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

  m_video_frame = av_frame_alloc();
  if (!m_video_frame) {
    return false;
  }
  m_video_frame->format = m_video_codec_context->pix_fmt;
  m_video_frame->width = m_video_codec_context->width;
  m_video_frame->height = m_video_codec_context->height;
  if (av_frame_get_buffer(m_video_frame, 0) < 0) {
    return false;
  }

  // TODO: handle variable width/height?
  m_sws_context = sws_getContext(
    m_video_codec_context->width,
    m_video_codec_context->height,
    AVPixelFormat::AV_PIX_FMT_RGBA,
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

bool UMR::Encoder::initialize_audio(
  AVCodecID codec_id,
  int sample_rate,
  int64_t bit_rate,
  int channels,
  int buffer_size
) {
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

  m_audio_frame = av_frame_alloc();
  if (!m_audio_frame) {
    return false;
  }
  m_audio_frame->format = m_audio_codec_context->sample_fmt;
  m_audio_frame->nb_samples = m_audio_codec_context->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE
    ? DEFAULT_AUDIO_FRAME_SIZE
    : m_audio_codec_context->frame_size;
  if (av_channel_layout_copy(&m_audio_frame->ch_layout, &m_audio_codec_context->ch_layout) < 0) {
    return false;
  }
  if (av_frame_get_buffer(m_audio_frame, 0) < 0) {
    return false;
  }

  // TODO: handle variable channel layout/sample rate?
  if (swr_alloc_set_opts2(
    &m_swr_context,
    &m_audio_codec_context->ch_layout,
    m_audio_codec_context->sample_fmt,
    m_audio_codec_context->sample_rate,
    &m_audio_codec_context->ch_layout,
    AVSampleFormat::AV_SAMPLE_FMT_FLT,
    m_audio_codec_context->sample_rate,
    0,
    nullptr
  ) < 0) {
    return false;
  }
  if (swr_init(m_swr_context) < 0) {
    return false;
  }

  m_audio_fifo = av_audio_fifo_alloc(
    m_audio_codec_context->sample_fmt,
    m_audio_codec_context->ch_layout.nb_channels,
    m_audio_frame->nb_samples
  );
  if (!m_audio_fifo) {
    return false;
  }

  // TODO: handle variable buffer size?
  m_audio_buffer_size = buffer_size;
  m_audio_buffers = new float* [m_audio_codec_context->ch_layout.nb_channels];
  for (int i = 0; i < m_audio_codec_context->ch_layout.nb_channels; i++) {
    m_audio_buffers[i] = new float[m_audio_buffer_size];
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

bool UMR::Encoder::encode(AVMediaType media_type, AVFrame* frame) {
  AVCodecContext* codec_context;
  switch (media_type) {
    case AVMediaType::AVMEDIA_TYPE_VIDEO:
    {
      codec_context = m_video_codec_context;
      break;
    }
    case AVMediaType::AVMEDIA_TYPE_AUDIO:
    {
      codec_context = m_audio_codec_context;
      break;
    }
    default:
    {
      return false;
    }
  }

  int stream_index = av_find_best_stream(m_format_context, codec_context->codec_type, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    return false;
  }
  AVStream* stream = m_format_context->streams[stream_index];

  if (avcodec_send_frame(codec_context, frame) != 0) {
    return false;
  }
  while (avcodec_receive_packet(codec_context, m_packet) == 0) {
    av_packet_rescale_ts(m_packet, codec_context->time_base, stream->time_base);
    m_packet->stream_index = stream->index;

    if (av_interleaved_write_frame(m_format_context, m_packet) < 0) {
      return false;
    }
  }

  return true;
}