#include <utility>

#include "Encoder.hpp"

constexpr int DEFAULT_AUDIO_FRAME_SIZE = 10000;
constexpr const char* H264_ENCODER_NAMES[] { // TODO: properly handle hardware encoding
  "h264_nvenc",
  "h264_amf",
  "h264_qsv",
#if defined(_WIN32)
  "h264_mf",
#elif defined(__APPLE__)
  "h264_videotoolbox",
#endif
  "libopenh264",
  nullptr
};

UMR::Encoder* UMR::Encoder::create(
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
) {
  if (!muxer) {
    return nullptr;
  }

  Encoder encoder;

  if (muxer->audio() && audio_codec_id != AVCodecID::AV_CODEC_ID_NONE) {
    if (!encoder.initialize_audio(muxer, audio_codec_id, sample_rate, audio_bit_rate, channels, audio_buffer_size)) {
      return nullptr;
    }
  }

  if (muxer->video() && video_codec_id != AVCodecID::AV_CODEC_ID_NONE) {
    if (!encoder.initialize_video(muxer, video_codec_id, width, height, video_bit_rate)) {
      return nullptr;
    }
  }

  if (!encoder.m_audio_codec_context && !encoder.m_video_codec_context) {
    return nullptr;
  }

  return new Encoder(std::move(encoder));
}

UMR::Encoder::~Encoder() {
  sws_freeContext(m_sws_context);

  if (m_video_frame) {
    av_frame_free(&m_video_frame);
  }

  if (m_video_codec_context) {
    avcodec_free_context(&m_video_codec_context);
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
}

std::vector<AVPacket*>* UMR::Encoder::encode_audio(float* data) {
  if (!m_audio_codec_context) {
    return nullptr;
  }

  if (swr_convert(
    m_swr_context,
    reinterpret_cast<uint8_t**>(m_audio_buffers),
    m_audio_buffer_size,
    reinterpret_cast<uint8_t**>(&data),
    m_audio_buffer_size
  ) != m_audio_buffer_size) {
    return nullptr;
  }

  if (av_audio_fifo_write(
    m_audio_fifo,
    reinterpret_cast<void**>(m_audio_buffers),
    m_audio_buffer_size
  ) != m_audio_buffer_size) {
    return nullptr;
  }

  if (av_audio_fifo_size(m_audio_fifo) >= m_audio_frame->nb_samples) {
    if (av_frame_make_writable(m_audio_frame) < 0) {
      return nullptr;
    }

    if (av_audio_fifo_read(
      m_audio_fifo,
      reinterpret_cast<void**>(m_audio_frame->data),
      m_audio_frame->nb_samples
    ) != m_audio_frame->nb_samples) {
      return nullptr;
    }
    m_audio_frame->pts = m_next_audio_pts;
    m_next_audio_pts += m_audio_frame->nb_samples;

    std::vector<AVPacket*> packets;

    if (!encode(&packets, m_audio_codec_context, m_audio_frame)) {
      return nullptr;
    }

    return new std::vector<AVPacket*>(std::move(packets));
  }

  return nullptr;
}

std::vector<AVPacket*>* UMR::Encoder::encode_video(int width, int height, uint8_t* data, int64_t pts) {
  if (!m_video_codec_context) {
    return nullptr;
  }

  if (av_frame_make_writable(m_video_frame) < 0) {
    return nullptr;
  }

  m_sws_context = sws_getCachedContext(
    m_sws_context,
    width,
    height,
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
    return nullptr;
  }

  int stride = width * 4;
  if (sws_scale(
    m_sws_context,
    &data,
    &stride,
    0,
    height,
    m_video_frame->data,
    m_video_frame->linesize
  ) != m_video_frame->height) {
    return nullptr;
  }
  m_video_frame->pts = pts;

  std::vector<AVPacket*> packets;

  if (!encode(&packets, m_video_codec_context, m_video_frame)) {
    return nullptr;
  }

  return new std::vector<AVPacket*>(std::move(packets));
}

std::vector<AVPacket*>* UMR::Encoder::flush() {
  std::vector<AVPacket*> packets;

  if (m_audio_codec_context) {
    int remaining_samples = av_audio_fifo_size(m_audio_fifo);
    if (remaining_samples > 0) {
      if (av_frame_make_writable(m_audio_frame) < 0) {
        return nullptr;
      }

      if (av_audio_fifo_read(
        m_audio_fifo,
        reinterpret_cast<void**>(m_audio_frame->data),
        remaining_samples
      ) != remaining_samples) {
        return nullptr;
      }
      m_audio_frame->nb_samples = remaining_samples;
      m_audio_frame->pts = m_next_audio_pts;

      if (!encode(&packets, m_audio_codec_context, m_audio_frame)) {
        return nullptr;
      }
    }

    if (!encode(&packets, m_audio_codec_context, nullptr)) {
      return nullptr;
    }
  }

  if (m_video_codec_context) {
    if (!encode(&packets, m_video_codec_context, nullptr)) {
      return nullptr;
    }
  }

  return new std::vector<AVPacket*>(std::move(packets));
}

UMR::Encoder::Encoder() {}

UMR::Encoder::Encoder(Encoder&& other) noexcept:
  m_audio_codec_context(other.m_audio_codec_context),
  m_audio_frame(other.m_audio_frame),
  m_swr_context(other.m_swr_context),
  m_audio_fifo(other.m_audio_fifo),
  m_audio_buffer_size(other.m_audio_buffer_size),
  m_audio_buffers(other.m_audio_buffers),
  m_next_audio_pts(other.m_next_audio_pts),
  m_video_codec_context(other.m_video_codec_context),
  m_video_frame(other.m_video_frame),
  m_sws_context(other.m_sws_context) {
  other.m_audio_codec_context = nullptr;
  other.m_audio_frame = nullptr;
  other.m_swr_context = nullptr;
  other.m_audio_fifo = nullptr;
  other.m_audio_buffer_size = 0;
  other.m_audio_buffers = nullptr;
  other.m_next_audio_pts = 0;
  other.m_video_codec_context = nullptr;
  other.m_video_frame = nullptr;
  other.m_sws_context = nullptr;
}

bool UMR::Encoder::initialize_audio(
  Muxer* muxer,
  AVCodecID codec_id,
  int sample_rate,
  int64_t bit_rate,
  int channels,
  int buffer_size
) {
  if (!muxer) {
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
  if (muxer->global_header()) {
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

  if (!muxer->add_stream(m_audio_codec_context)) {
    return false;
  }

  return true;
}

bool UMR::Encoder::initialize_video(
  Muxer* muxer,
  AVCodecID codec_id,
  int width,
  int height,
  int64_t bit_rate
  // int gop_size
  // int max_b_frames
) {
  if (!muxer) {
    return false;
  }

  std::vector<const AVCodec*> codecs;
  if (codec_id == AVCodecID::AV_CODEC_ID_H264) {
    for (const char* const* h264_encoder_name = H264_ENCODER_NAMES; *h264_encoder_name != nullptr; h264_encoder_name++) {
      codecs.push_back(avcodec_find_encoder_by_name(*h264_encoder_name));
    }
  } else {
    codecs.push_back(avcodec_find_encoder(codec_id));
  }

  for (const AVCodec* codec : codecs) {
    if (!codec) {
      continue;
    }

    m_video_codec_context = avcodec_alloc_context3(codec);
    if (!m_video_codec_context) {
      continue;
    }
    m_video_codec_context->width = width;
    m_video_codec_context->height = height;
    m_video_codec_context->bit_rate = bit_rate;
    m_video_codec_context->time_base = {.num = 1, .den = 1000};
    // m_codec_context->gop_size = gop_size;
    // m_codec_context->max_b_frames = max_b_frames;
    m_video_codec_context->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
    if (muxer->global_header()) {
      m_video_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (avcodec_open2(m_video_codec_context, codec, nullptr) >= 0) {
      break;
    }
    avcodec_free_context(&m_video_codec_context);
  }
  if (!m_video_codec_context) {
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

  if (!muxer->add_stream(m_video_codec_context)) {
    return false;
  }

  return true;
}

bool UMR::Encoder::encode(std::vector<AVPacket*>* packets, AVCodecContext* codec_context, AVFrame* frame) {
  if (!packets) {
    return false;
  }

  if (!codec_context) {
    return false;
  }

  if (avcodec_send_frame(codec_context, frame) != 0) {
    return false;
  }

  int ret = 0;
  while (ret == 0) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
      return false;
    }

    ret = avcodec_receive_packet(codec_context, packet);
    if (ret == 0) {
      packet->opaque = codec_context;

      packets->push_back(packet);
    } else {
      av_packet_free(&packet);
    }
  }

  return true;
}