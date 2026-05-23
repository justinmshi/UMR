#pragma once

#include <cstdint>

#if defined(_WIN32)
#if defined(UMR_ENCODE_API_EXPORT)
#define UMR_ENCODE_API __declspec(dllexport)
#else
#define UMR_ENCODE_API __declspec(dllimport)
#endif
#else
#define UMR_ENCODE_API
#endif

extern "C" {
  UMR_ENCODE_API void* umr_encode_begin(
    const char* filename,
    int32_t codec_id,
    int32_t width,
    int32_t height,
    int64_t bit_rate
  );
  UMR_ENCODE_API uint8_t umr_encode_encode(void* encoder, uint8_t* data, int64_t pts);
  UMR_ENCODE_API uint8_t umr_encode_end(void** encoder);
}