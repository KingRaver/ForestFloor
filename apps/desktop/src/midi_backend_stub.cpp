#include "midi_backend.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ff::desktop {
namespace {

class NullMidiBackend final : public MidiBackend {
 public:
  bool start(const std::string& /*preferred_device_id*/,
             MessageCallback callback,
             std::string* error_message) override {
    if (callback == nullptr) {
      if (error_message != nullptr) {
        *error_message = "invalid MIDI callback";
      }
      return false;
    }
    callback_ = std::move(callback);
    running_.store(true, std::memory_order_release);
    return true;
  }

  void stop() override {
    running_.store(false, std::memory_order_release);
    callback_ = {};
  }

  [[nodiscard]] bool isRunning() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::vector<MidiDeviceInfo> inputDevices() const override {
    return {
        MidiDeviceInfo{.id = "none", .name = "No MIDI inputs available"},
    };
  }

 private:
  std::atomic<bool> running_{false};
  MessageCallback callback_{};
};

}  // namespace

#if !defined(__APPLE__)
std::unique_ptr<MidiBackend> createMidiBackend() {
  return std::make_unique<NullMidiBackend>();
}
#endif

}  // namespace ff::desktop
