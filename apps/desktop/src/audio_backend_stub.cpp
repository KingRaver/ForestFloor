#include "audio_backend.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace ff::desktop {
namespace {

class SimulatedAudioBackend final : public AudioBackend {
 public:
  bool start(const AudioBackendConfig& config,
             Callback callback,
             std::string* error_message) override {
    if (running_.load(std::memory_order_acquire)) {
      return true;
    }

    if (config.sample_rate_hz == 0 || config.buffer_size_frames == 0 ||
        callback == nullptr) {
      if (error_message != nullptr) {
        *error_message = "invalid simulated audio backend configuration";
      }
      return false;
    }

    config_ = config;
    callback_ = std::move(callback);

    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this]() { run(); });
    return true;
  }

  void stop() override {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    if (worker_.joinable()) {
      worker_.join();
    }
  }

  [[nodiscard]] bool isRunning() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::vector<AudioDeviceInfo> outputDevices() const override {
    return {
        AudioDeviceInfo{.id = "default", .name = "Simulated Output", .is_default = true},
    };
  }

  [[nodiscard]] AudioBackendStats stats() const noexcept override {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
  }

 private:
  void run() {
    std::vector<float> interleaved(config_.buffer_size_frames * 2U, 0.0F);

    const double budget_us =
        static_cast<double>(config_.buffer_size_frames) * 1'000'000.0 /
        static_cast<double>(config_.sample_rate_hz);

    std::chrono::steady_clock::time_point last_callback_time{};

    while (running_.load(std::memory_order_acquire)) {
      const auto callback_start = std::chrono::steady_clock::now();
      callback_(interleaved.data(), config_.buffer_size_frames);
      const auto callback_end = std::chrono::steady_clock::now();

      const double callback_duration_us =
          static_cast<double>(
              std::chrono::duration_cast<std::chrono::microseconds>(
                  callback_end - callback_start)
                  .count());

      double callback_interval_us = 0.0;
      if (last_callback_time.time_since_epoch().count() != 0) {
        callback_interval_us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                callback_start - last_callback_time)
                .count());
      }
      last_callback_time = callback_start;

      {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.callback_count += 1;
        stats_.average_callback_duration_us +=
            (callback_duration_us - stats_.average_callback_duration_us) /
            static_cast<double>(stats_.callback_count);
        stats_.peak_callback_duration_us =
            std::max(stats_.peak_callback_duration_us, callback_duration_us);

        if (callback_interval_us > 0.0) {
          stats_.average_callback_interval_us +=
              (callback_interval_us - stats_.average_callback_interval_us) /
              static_cast<double>(stats_.callback_count);
          stats_.peak_callback_interval_us =
              std::max(stats_.peak_callback_interval_us, callback_interval_us);
        }

        if (callback_duration_us > (budget_us * 0.95)) {
          stats_.xrun_count += 1;
        }
      }

      const auto elapsed = std::chrono::steady_clock::now() - callback_start;
      const auto target = std::chrono::microseconds(
          static_cast<std::int64_t>(std::llround(budget_us)));
      if (elapsed < target) {
        std::this_thread::sleep_for(target - elapsed);
      }
    }
  }

  AudioBackendConfig config_{};
  Callback callback_{};

  std::atomic<bool> running_{false};
  std::thread worker_;

  mutable std::mutex stats_mutex_;
  AudioBackendStats stats_{};
};

}  // namespace

#if !defined(__APPLE__)
std::unique_ptr<AudioBackend> createAudioBackend() {
  return std::make_unique<SimulatedAudioBackend>();
}
#endif

}  // namespace ff::desktop
