#include <cstdint>
#include <numbers>
#include <algorithm>
#include <cmath>
#include <encode/api.hpp>
#include <iostream>

constexpr int64_t COLOR_PERIOD = 5 * 1000;
constexpr int32_t WIDTH = 1920;
constexpr int32_t HEIGHT = 1080;
constexpr int64_t DURATION = 10 * 1000;
constexpr int64_t FRAME_TIME = 1000 / 30;

static void generate_data(uint8_t* data, int64_t pts) {
  double radians = 2 * std::numbers::pi_v<double> *pts / COLOR_PERIOD;
  for (int i = 0; i < WIDTH * HEIGHT; i++) {
    data[i * 4] = std::clamp(static_cast<int>(lround(sin(radians) * 127 + 128)), 0, 255);
    data[i * 4 + 1] = std::clamp(static_cast<int>(lround(sin(radians + 2 * std::numbers::pi_v<double> / 3) * 127 + 128)), 0, 255);
    data[i * 4 + 2] = std::clamp(static_cast<int>(lround(sin(radians + 4 * std::numbers::pi_v<double> / 3) * 127 + 128)), 0, 255);
    data[i * 4 + 3] = 255;
  }
}

int main() {
  uint8_t* data = new uint8_t[WIDTH * HEIGHT * 4];

  void* encoder = umr_encode_create(WIDTH, HEIGHT);
  std::cout << "umr_encode create: " << (encoder ? "success" : "failure") << "\n";

  std::cout << "umr_encode begin: " << (umr_encode_begin(encoder, "umr_example.mp4") ? "success" : "failure") << "\n";

  for (int64_t pts = 0; pts < DURATION; pts += FRAME_TIME) {
    generate_data(data, pts);
    std::cout << "umr_encode encode (pts: " << pts << "): " << (umr_encode_encode(encoder, data, pts) ? "success" : "failure") << "\n";
  }

  std::cout << "umr_encode end: " << (umr_encode_end(encoder) ? "success" : "failure") << "\n";

  umr_encode_destroy(encoder);

  delete[] data;
}