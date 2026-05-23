#include "api.hpp"
#include "Encoder.hpp"

void* umr_encode_begin(
  const char* filename,
  int32_t codec_id,
  int32_t width,
  int32_t height,
  int64_t bit_rate
) {
  return UMR::Encoder::begin(
    filename,
    static_cast<AVCodecID>(codec_id),
    width,
    height,
    bit_rate
  );
}

uint8_t umr_encode_encode(void* encoder, uint8_t* data, int64_t pts) {
  if (!encoder) {
    return false;
  }

  return static_cast<UMR::Encoder*>(encoder)->encode(data, pts);
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