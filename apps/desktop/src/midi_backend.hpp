#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ff::desktop {

struct MidiDeviceInfo final {
  std::string id;
  std::string name;
};

class MidiBackend {
 public:
  using MessageCallback = std::function<void(const std::uint8_t* bytes,
                                             std::size_t size)>;

  virtual ~MidiBackend() = default;

  virtual bool start(const std::string& preferred_device_id,
                     MessageCallback callback,
                     std::string* error_message = nullptr) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual bool isRunning() const noexcept = 0;

  [[nodiscard]] virtual std::vector<MidiDeviceInfo> inputDevices() const = 0;
};

[[nodiscard]] std::unique_ptr<MidiBackend> createMidiBackend();

}  // namespace ff::desktop
