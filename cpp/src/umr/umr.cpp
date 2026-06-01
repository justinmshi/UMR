#include "umr.hpp"
#include "Muxer.hpp"
#include "Encoder.hpp"

uint8_t umr_begin(
  void** muxer,
  const char* filename,
  void** encoder,
  int32_t audio_codec_id,
  int32_t sample_rate,
  int64_t audio_bit_rate,
  int32_t channels,
  int32_t audio_buffer_size,
  int32_t video_codec_id,
  int32_t width,
  int32_t height,
  int64_t video_bit_rate
) {
  *muxer = UMR::Muxer::create(filename);
  if (!*muxer) {
    return false;
  }

  UMR::Muxer* cast_muxer = static_cast<UMR::Muxer*>(*muxer);

  *encoder = UMR::Encoder::create(
    cast_muxer,
    static_cast<AVCodecID>(audio_codec_id),
    sample_rate,
    audio_bit_rate,
    channels,
    audio_buffer_size,
    static_cast<AVCodecID>(video_codec_id),
    width,
    height,
    video_bit_rate
  );
  if (!*encoder) {
    delete cast_muxer;
    *muxer = nullptr;

    return false;
  }

  return cast_muxer->begin();
}

void* umr_encode_audio(void* encoder, float* data) {
  if (!encoder) {
    return nullptr;
  }

  return static_cast<UMR::Encoder*>(encoder)->encode_audio(data);
}

void* umr_encode_video(void* encoder, uint8_t* data, int64_t pts) {
  if (!encoder) {
    return nullptr;
  }

  return static_cast<UMR::Encoder*>(encoder)->encode_video(data, pts);
}

uint8_t umr_mux(void* muxer, void* packets) {
  if (!muxer) {
    return false;
  }

  if (!packets) {
    return true;
  }

  std::vector<AVPacket*>* cast_packets = static_cast<std::vector<AVPacket*>*>(packets);
  UMR::Muxer* cast_muxer = static_cast<UMR::Muxer*>(muxer);

  bool success = true;
  for (int i = 0; i < cast_packets->size(); i++) {
    if (!cast_muxer->mux((*cast_packets)[i])) {
      success = false;
    }

    av_packet_free(&(*cast_packets)[i]);
  }

  delete cast_packets;

  return success;
}

uint8_t umr_end(void** encoder, void** muxer) {
  if (!encoder || !*encoder) {
    return false;
  }

  if (!muxer || !*muxer) {
    return false;
  }

  UMR::Encoder* cast_encoder = static_cast<UMR::Encoder*>(*encoder);
  std::vector<AVPacket*>* packets = cast_encoder->flush();
  UMR::Muxer* cast_muxer = static_cast<UMR::Muxer*>(*muxer);

  bool success = true;
  if (packets) {
    for (int i = 0; i < packets->size(); i++) {
      if (!cast_muxer->mux((*packets)[i])) {
        success = false;
      }

      av_packet_free(&(*packets)[i]);
    }

    delete packets;
  }

  if (!cast_muxer->end()) {
    return false;
  }

  delete cast_encoder;
  *encoder = nullptr;

  delete cast_muxer;
  *muxer = nullptr;

  return success;
}