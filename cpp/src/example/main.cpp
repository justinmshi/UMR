#include <cstdint>
#include <numbers>
#include <cmath>
#include <algorithm>
#include <umr.hpp>
#include <iostream>

constexpr int32_t AUDIO_BUFFER_SIZE = 1024;
constexpr int32_t SAMPLE_RATE = 44100;
constexpr int64_t PERIOD = 5 * 1000;
constexpr int64_t AUDIO_FREQUENCY = 200;
constexpr int32_t CHANNELS = 2;
constexpr int32_t WIDTH = 1920;
constexpr int32_t HEIGHT = 1080;
constexpr int32_t AUDIO_CODEC_ID = 86018;
constexpr int64_t AUDIO_BIT_RATE = 128000;
constexpr int32_t VIDEO_CODEC_ID = 27;
constexpr int64_t VIDEO_BIT_RATE = 8000000;
constexpr int64_t DURATION = 10 * 1000;
constexpr int64_t FRAME_TIME = 1000 / 30;

static void generate_audio_data(float* data, int64_t pts) {
  for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
    double t = 1. * (pts + i) / SAMPLE_RATE;
    double volume_radians = 2 * std::numbers::pi_v<double> *t * 1000 / PERIOD;
    double signal_radians = 2 * std::numbers::pi_v<double> *t * AUDIO_FREQUENCY;
    for (int j = 0; j < CHANNELS; j++) {
      data[i * CHANNELS + j] = (sin(volume_radians + 2 * std::numbers::pi_v<double> *j / CHANNELS) + 1) / 2 * sin(signal_radians);
    }
  }
}

static void generate_video_data(uint8_t* data, int64_t pts) {
  double radians = 2 * std::numbers::pi_v<double> *pts / PERIOD;
  for (int i = 0; i < WIDTH * HEIGHT; i++) {
    for (int j = 0; j < 3; j++) {
      data[i * 4 + j] = std::clamp(static_cast<int>(lround(sin(radians + 2 * std::numbers::pi_v<double> *j / 3) * 127 + 128)), 0, 255);
    }
    data[i * 4 + 3] = 255;
  }
}

static void generate_media_file(bool audio, bool video) {
  if (!audio && !video) {
    return;
  }

  void* muxer;
  const char* filename = audio && video ? "example_av.mp4" : audio ? "example_a.m4a" : "example_v.mp4";
  void* encoder;
  bool begin_success = umr_begin(
    &muxer,
    filename,
    &encoder,
    audio ? AUDIO_CODEC_ID : 0,
    audio ? SAMPLE_RATE : 0,
    audio ? AUDIO_BIT_RATE : 0,
    audio ? CHANNELS : 0,
    video ? VIDEO_CODEC_ID : 0,
    video ? WIDTH : 0,
    video ? HEIGHT : 0,
    video ? VIDEO_BIT_RATE : 0
  );
  std::cout << "umr begin (" << filename << "): " << (begin_success ? "success" : "failure") << "\n";

  if (audio) {
    float* audio_data = new float[CHANNELS * AUDIO_BUFFER_SIZE];
    for (int64_t pts = 0; pts < SAMPLE_RATE * DURATION / 1000; pts += AUDIO_BUFFER_SIZE) {
      generate_audio_data(audio_data, pts);
      void* packets = umr_encode_audio(encoder, CHANNELS, SAMPLE_RATE, AUDIO_BUFFER_SIZE, audio_data);
      bool mux_audio_success = umr_mux(muxer, packets);
      std::cout << "umr mux audio (" << pts << "): " << (mux_audio_success ? "success" : "failure") << "\n";
    }
    delete[] audio_data;
  }

  if (video) {
    uint8_t* video_data = new uint8_t[WIDTH * HEIGHT * 4];
    for (int64_t pts = 0; pts < DURATION; pts += FRAME_TIME) {
      generate_video_data(video_data, pts);
      void* packets = umr_encode_video(encoder, WIDTH, HEIGHT, video_data, pts);
      bool mux_video_success = umr_mux(muxer, packets);
      std::cout << "umr mux video (" << pts << "): " << (mux_video_success ? "success" : "failure") << "\n";
    }
    delete[] video_data;
  }

  bool end_success = umr_end(&encoder, &muxer);
  std::cout << "umr end: " << (end_success ? "success" : "failure") << "\n";
  std::cout << "encoder: " << encoder << "\n";
  std::cout << "muxer: " << muxer << "\n";
}

int main() {
  generate_media_file(true, true);
  generate_media_file(true, false);
  generate_media_file(false, true);

  return 0;
}