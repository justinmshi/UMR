#pragma once

#include <cstdint>

#if defined(_WIN32)
#if defined(UMR_API_EXPORT)
#define UMR_API __declspec(dllexport)
#else
#define UMR_API __declspec(dllimport)
#endif
#else
#define UMR_API
#endif

extern "C" {
  UMR_API uint8_t umr_begin(
    void** muxer,
    const char* filename,
    void** encoder,
    int32_t audio_codec_id,
    int32_t sample_rate,
    int64_t audio_bit_rate,
    int32_t channels,
    int32_t video_codec_id,
    int32_t width,
    int32_t height,
    int64_t video_bit_rate
  );
  UMR_API void* umr_encode_audio(void* encoder, int32_t channels, int32_t sample_rate, int32_t samples, float* data);
  UMR_API void* umr_encode_video(void* encoder, int32_t width, int32_t height, uint8_t* data, int64_t pts);
  UMR_API uint8_t umr_mux(void* muxer, void* packets);
  UMR_API uint8_t umr_end(void** encoder, void** muxer);
}