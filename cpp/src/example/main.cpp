#include <cstdint>
#include <numbers>
#include <algorithm>
#include <cmath>
#include <encode/api.hpp>
#include <iostream>

constexpr int64_t COLOR_PERIOD = 5 * 1000;
constexpr int32_t WIDTH = 1920;
constexpr int32_t HEIGHT = 1080;
constexpr int32_t AUDIO_BUFFER_SIZE = 1024;
constexpr int32_t SAMPLE_RATE = 44100;
constexpr int64_t AUDIO_FREQUENCY = 200;
constexpr int32_t CHANNELS = 2;
constexpr int32_t VIDEO_CODEC_ID = 27;
constexpr int64_t VIDEO_BIT_RATE = 8000000;
constexpr int32_t AUDIO_CODEC_ID = 86018;
constexpr int64_t AUDIO_BIT_RATE = 128000;
constexpr int64_t DURATION = 10 * 1000;
constexpr int64_t FRAME_TIME = 1000 / 30;

static void generate_video_data(uint8_t* data, int64_t pts) {
  double radians = 2 * std::numbers::pi_v<double> *pts / COLOR_PERIOD;
  for (int i = 0; i < WIDTH * HEIGHT; i++) {
    data[i * 4] = std::clamp(static_cast<int>(lround(sin(radians) * 127 + 128)), 0, 255);
    data[i * 4 + 1] = std::clamp(static_cast<int>(lround(sin(radians + 2 * std::numbers::pi_v<double> / 3) * 127 + 128)), 0, 255);
    data[i * 4 + 2] = std::clamp(static_cast<int>(lround(sin(radians + 4 * std::numbers::pi_v<double> / 3) * 127 + 128)), 0, 255);
    data[i * 4 + 3] = 255;
  }
}

static void generate_audio_data(float* data, int64_t pts) {
  for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
    double t = 1. * (pts + i) / SAMPLE_RATE;
    double volume_radians = 2 * std::numbers::pi_v<double> *t * 1000 / COLOR_PERIOD;
    double signal_radians = 2 * std::numbers::pi_v<double> *t * AUDIO_FREQUENCY;
    for (int j = 0; j < CHANNELS; j++) {
      data[i * CHANNELS + j] = (sin(volume_radians + j * std::numbers::pi_v<double> / CHANNELS) + 1) / 2 * sin(signal_radians);
    }
  }
}

static void generate(bool video, bool audio) {
  if (!video && !audio) {
    return;
  }

  const char* filename = video && audio ? "umr_example_av.mp4" : video ? "umr_example_v.mp4" : "umr_example_a.m4a";
  void* encoder = umr_encode_begin(
    filename,
    video ? VIDEO_CODEC_ID : 0,
    video ? WIDTH : 0,
    video ? HEIGHT : 0,
    video ? VIDEO_BIT_RATE : 0,
    audio ? AUDIO_CODEC_ID : 0,
    audio ? SAMPLE_RATE : 0,
    audio ? AUDIO_BIT_RATE : 0,
    audio ? CHANNELS : 0,
    audio ? AUDIO_BUFFER_SIZE : 0
  );
  std::cout << "umr_encode begin (" << filename << "): " << (encoder ? "success" : "failure") << "\n";

  if (video) {
    uint8_t* video_data = new uint8_t[WIDTH * HEIGHT * 4];
    for (int64_t pts = 0; pts < DURATION; pts += FRAME_TIME) {
      generate_video_data(video_data, pts);
      bool send_video_success = umr_encode_send_video(encoder, video_data, pts);
      std::cout << "umr_encode send video (pts: " << pts << "): " << (send_video_success ? "success" : "failure") << "\n";
    }
    delete[] video_data;
  }

  if (audio) {
    float* audio_data = new float[CHANNELS * AUDIO_BUFFER_SIZE];
    for (int64_t pts = 0; pts < SAMPLE_RATE * DURATION / 1000; pts += AUDIO_BUFFER_SIZE) {
      generate_audio_data(audio_data, pts);
      bool send_audio_success = umr_encode_send_audio(encoder, audio_data);
      std::cout << "umr_encode send audio (pts: " << pts << "): " << (send_audio_success ? "success" : "failure") << "\n";
    }
    delete[] audio_data;
  }

  bool end_success = umr_encode_end(&encoder);
  std::cout << "umr_encode end: " << (end_success ? "success" : "failure") << "\n";
  std::cout << "encoder: " << encoder << "\n";
}

int main() {
  generate(true, true);
  generate(true, false);
  generate(false, true);
}