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
    int32_t video_codec_id,
    int32_t width,
    int32_t height,
    int64_t video_bit_rate,
    int32_t audio_codec_id,
    int32_t sample_rate,
    int64_t audio_bit_rate,
    int32_t channels,
    int32_t audio_buffer_size
  );
  UMR_ENCODE_API uint8_t umr_encode_send_video(void* encoder, uint8_t* data, int64_t pts);
  UMR_ENCODE_API uint8_t umr_encode_send_audio(void* encoder, float* data);
  UMR_ENCODE_API uint8_t umr_encode_end(void** encoder);
}