#include "api.hpp"
#include "Encoder.hpp"

void* umr_encode_begin(
  const char* filename,
  int32_t video_codec_id,
  int32_t width,
  int32_t height,
  int64_t video_bit_rate,
  int32_t audio_codec_id,
  int32_t sample_rate,
  int64_t audio_bit_rate,
  int32_t channels,
  int32_t audio_buffer_size
) {
  return UMR::Encoder::begin(
    filename,
    static_cast<AVCodecID>(video_codec_id),
    width,
    height,
    video_bit_rate,
    static_cast<AVCodecID>(audio_codec_id),
    sample_rate,
    audio_bit_rate,
    channels,
    audio_buffer_size
  );
}

uint8_t umr_encode_send_video(void* encoder, uint8_t* data, int64_t pts) {
  if (!encoder) {
    return false;
  }

  return static_cast<UMR::Encoder*>(encoder)->send_video(data, pts);
}

uint8_t umr_encode_send_audio(void* encoder, float* data) {
  if (!encoder) {
    return false;
  }

  return static_cast<UMR::Encoder*>(encoder)->send_audio(data);
}

uint8_t umr_encode_end(void** encoder) {
  if (!encoder || !*encoder) {
    return false;
  }

  UMR::Encoder* cast_encoder = static_cast<UMR::Encoder*>(*encoder);

  if (!cast_encoder->end()) {
    return false;
  }

  delete cast_encoder;
  *encoder = nullptr;

  return true;
}