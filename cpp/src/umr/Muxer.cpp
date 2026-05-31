#include <utility>

#include "Muxer.hpp"

UMR::Muxer* UMR::Muxer::create(const char* filename) {
  Muxer muxer;

  if (avformat_alloc_output_context2(&muxer.m_format_context, nullptr, nullptr, filename) < 0) {
    return nullptr;
  }
  if (!(muxer.m_format_context->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&muxer.m_format_context->pb, muxer.m_format_context->url, AVIO_FLAG_WRITE) < 0) {
      return nullptr;
    }
  }

  return new Muxer(std::move(muxer));
}

UMR::Muxer::~Muxer() {
  if (m_format_context) {
    if (m_format_context->pb) {
      avio_closep(&m_format_context->pb);
    }
    avformat_free_context(m_format_context);
  }
}

bool UMR::Muxer::begin() {
  return avformat_write_header(m_format_context, nullptr) >= 0;
}

bool UMR::Muxer::mux(AVPacket* packet) {
  if (!packet || !packet->opaque) {
    return false;
  }

  AVCodecContext* codec_context = static_cast<AVCodecContext*>(packet->opaque);
  int stream_index = av_find_best_stream(m_format_context, codec_context->codec_type, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    return false;
  }
  AVStream* stream = m_format_context->streams[stream_index];
  av_packet_rescale_ts(packet, codec_context->time_base, stream->time_base);
  packet->stream_index = stream->index;

  return av_interleaved_write_frame(m_format_context, packet) >= 0;
}

bool UMR::Muxer::end() {
  return av_write_trailer(m_format_context) == 0;
}

bool UMR::Muxer::audio() {
  return m_format_context->oformat->audio_codec != AVCodecID::AV_CODEC_ID_NONE;
}

bool UMR::Muxer::video() {
  return m_format_context->oformat->video_codec != AVCodecID::AV_CODEC_ID_NONE;
}

bool UMR::Muxer::global_header() {
  return m_format_context->oformat->flags & AVFMT_GLOBALHEADER;
}

bool UMR::Muxer::add_stream(AVCodecContext* codec_context) {
  if (!codec_context) {
    return false;
  }

  AVStream* stream = avformat_new_stream(m_format_context, nullptr);
  if (!stream) {
    return false;
  }
  stream->id = m_format_context->nb_streams - 1;
  stream->time_base = codec_context->time_base;
  if (avcodec_parameters_from_context(stream->codecpar, codec_context) < 0) {
    return false;
  }

  return true;
}

UMR::Muxer::Muxer() {}

UMR::Muxer::Muxer(Muxer&& other) noexcept: m_format_context(other.m_format_context) {
  other.m_format_context = nullptr;
}