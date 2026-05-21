#include "api.hpp"
#include "Encoder.hpp"

void* umr_encode_create(int32_t width, int32_t height) {
  return UMR::Encoder::create(width, height);
}

uint8_t umr_encode_begin(void* encoder, const char* filename) {
  if (!encoder) {
    return false;
  }

  return static_cast<UMR::Encoder*>(encoder)->begin(filename);
}

uint8_t umr_encode_encode(void* encoder, uint8_t* data, int64_t pts) {
  if (!encoder) {
    return false;
  }

  return static_cast<UMR::Encoder*>(encoder)->encode(data, pts);
}

uint8_t umr_encode_end(void* encoder) {
  if (!encoder) {
    return false;
  }

  return static_cast<UMR::Encoder*>(encoder)->end();
}

void umr_encode_destroy(void* encoder) {
  delete encoder;
}