#include "midi_backend.hpp"

#if defined(APPLE)

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

namespace ff::desktop {
namespace {

std::string cfStringToUtf8(CFStringRef value) {
  if (value == nullptr) {
    return {};
  }

  const CFIndex length = CFStringGetLength(value);
  const CFIndex max_size =
      CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::string output(static_cast<std::size_t>(max_size), '\0');
  if (!CFStringGetCString(value, output.data(), max_size, kCFStringEncodingUTF8)) {
    return {};
  }
  output.resize(std::strlen(output.c_str()));
  return output;
}

std::string sourceName(MIDIEndpointRef source, std::size_t fallback_index) {
  if (source == 0) {
    return "MIDI Source " + std::to_string(fallback_index);
  }

  CFStringRef name = nullptr;
  if (MIDIObjectGetStringProperty(source, kMIDIPropertyDisplayName, &name) == noErr &&
      name != nullptr) {
    const auto output = cfStringToUtf8(name);
    CFRelease(name);
    if (!output.empty()) {
      return output;
    }
  }

  return "MIDI Source " + std::to_string(fallback_index);
}

class CoreMidiBackend final : public MidiBackend {
 public:
  ~CoreMidiBackend() override { stop(); }

  bool start(const std::string& preferred_device_id,
             MessageCallback callback,
             std::string* error_message) override {
    if (running_.load(std::memory_order_acquire)) {
      return true;
    }

    if (callback == nullptr) {
      if (error_message != nullptr) {
        *error_message = "invalid MIDI callback";
      }
      return false;
    }

    OSStatus status = MIDIClientCreate(CFSTR("ForestFloorMIDIClient"), nullptr, nullptr,
                                       &client_);
    if (status != noErr || client_ == 0) {
      if (error_message != nullptr) {
        *error_message = "failed creating CoreMIDI client";
      }
      return false;
    }

    status = MIDIInputPortCreate(client_,
                                 CFSTR("ForestFloorMIDIInput"),
                                 &CoreMidiBackend::readProc,
                                 this,
                                 &input_port_);
    if (status != noErr || input_port_ == 0) {
      cleanup();
      if (error_message != nullptr) {
        *error_message = "failed creating CoreMIDI input port";
      }
      return false;
    }

    callback_ = std::move(callback);

    const auto sources = availableSources();
    bool connected = false;
    for (const auto& source : sources) {
      const bool selected = preferred_device_id.empty() || preferred_device_id == "default" ||
                            source.id == preferred_device_id ||
                            source.name.find(preferred_device_id) != std::string::npos;
      if (!selected) {
        continue;
      }

      const OSStatus connect_status = MIDIPortConnectSource(input_port_, source.endpoint,
                                                            nullptr);
      if (connect_status == noErr) {
        connected_sources_.push_back(source.endpoint);
        connected = true;
      }
    }

    if (!connected && !sources.empty()) {
      for (const auto& source : sources) {
        if (MIDIPortConnectSource(input_port_, source.endpoint, nullptr) == noErr) {
          connected_sources_.push_back(source.endpoint);
          connected = true;
        }
      }
    }

    if (!connected_sources_.empty()) {
      running_.store(true, std::memory_order_release);
      return true;
    }

    cleanup();
    if (error_message != nullptr) {
      *error_message = "no CoreMIDI input sources available";
    }
    return false;
  }

  void stop() override {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      cleanup();
      return;
    }

    cleanup();
  }

  [[nodiscard]] bool isRunning() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::vector<MidiDeviceInfo> inputDevices() const override {
    std::vector<MidiDeviceInfo> devices;
    const ItemCount count = MIDIGetNumberOfSources();
    devices.reserve(static_cast<std::size_t>(count));

    for (ItemCount index = 0; index < count; ++index) {
      const MIDIEndpointRef source = MIDIGetSource(index);
      devices.push_back(MidiDeviceInfo{
          .id = std::to_string(index),
          .name = sourceName(source, static_cast<std::size_t>(index)),
      });
    }

    if (devices.empty()) {
      devices.push_back(MidiDeviceInfo{.id = "none", .name = "No MIDI inputs"});
    }

    return devices;
  }

 private:
  struct SourceInfo final {
    std::string id;
    std::string name;
    MIDIEndpointRef endpoint = 0;
  };

  [[nodiscard]] std::vector<SourceInfo> availableSources() const {
    std::vector<SourceInfo> sources;
    const ItemCount count = MIDIGetNumberOfSources();
    sources.reserve(static_cast<std::size_t>(count));

    for (ItemCount index = 0; index < count; ++index) {
      const MIDIEndpointRef source = MIDIGetSource(index);
      if (source == 0) {
        continue;
      }
      sources.push_back(SourceInfo{
          .id = std::to_string(index),
          .name = sourceName(source, static_cast<std::size_t>(index)),
          .endpoint = source,
      });
    }

    return sources;
  }

  static void readProc(const MIDIPacketList* packet_list,
                       void* read_proc_ref_con,
                       void* /*src_conn_ref_con*/) {
    auto* self = reinterpret_cast<CoreMidiBackend*>(read_proc_ref_con);
    if (self == nullptr || packet_list == nullptr) {
      return;
    }

    self->dispatchPacketList(packet_list);
  }

  void dispatchPacketList(const MIDIPacketList* packet_list) {
    MessageCallback callback_copy;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback_copy = callback_;
    }

    if (callback_copy == nullptr) {
      return;
    }

    const MIDIPacket* packet = &packet_list->packet[0];
    for (UInt32 index = 0; index < packet_list->numPackets; ++index) {
      callback_copy(packet->data, packet->length);
      packet = MIDIPacketNext(packet);
    }
  }

  void cleanup() {
    for (MIDIEndpointRef source : connected_sources_) {
      if (input_port_ != 0) {
        MIDIPortDisconnectSource(input_port_, source);
      }
    }
    connected_sources_.clear();

    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback_ = {};
    }

    if (input_port_ != 0) {
      MIDIPortDispose(input_port_);
      input_port_ = 0;
    }
    if (client_ != 0) {
      MIDIClientDispose(client_);
      client_ = 0;
    }
  }

  MIDIClientRef client_ = 0;
  MIDIPortRef input_port_ = 0;
  std::vector<MIDIEndpointRef> connected_sources_;

  std::atomic<bool> running_{false};

  mutable std::mutex callback_mutex_;
  MessageCallback callback_{};
};

}  // namespace

std::unique_ptr<MidiBackend> createMidiBackend() {
  return std::make_unique<CoreMidiBackend>();
}

}  // namespace ff::desktop

#endif
