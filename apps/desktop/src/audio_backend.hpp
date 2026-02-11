#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ff::desktop {

struct AudioDeviceInfo final {
  std::string id;
  std::string name;
  bool is_default = false;
};

struct AudioBackendConfig final {
  std::string device_id = "default";
  std::uint32_t sample_rate_hz = 48'000;
  std::uint32_t buffer_size_frames = 256;
};

struct AudioBackendStats final {
  std::uint64_t callback_count = 0;
  std::uint64_t xrun_count = 0;
  double average_callback_duration_us = 0.0;
  double peak_callback_duration_us = 0.0;
  double average_callback_interval_us = 0.0;
  double peak_callback_interval_us = 0.0;
};

class AudioBackend {
 public:
  using Callback = std::function<void(float* interleaved_stereo_output,
                                      std::uint32_t frames)>;

  virtual ~AudioBackend() = default;

  virtual bool start(const AudioBackendConfig& config, Callback callback,
                     std::string* error_message = nullptr) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual bool isRunning() const noexcept = 0;

  [[nodiscard]] virtual std::vector<AudioDeviceInfo> outputDevices() const = 0;
  [[nodiscard]] virtual AudioBackendStats stats() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t actualSampleRate() const noexcept { return 0; }
};

[[nodiscard]] std::unique_ptr<AudioBackend> createAudioBackend();

}  // namespace ff::desktop
