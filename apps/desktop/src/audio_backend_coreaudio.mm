#include "audio_backend.hpp"

#if defined(__APPLE__)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

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

bool getDeviceName(AudioDeviceID device_id, std::string* name) {
  if (name == nullptr) {
    return false;
  }

  AudioObjectPropertyAddress address{
      .mSelector = kAudioObjectPropertyName,
      .mScope = kAudioObjectPropertyScopeGlobal,
      .mElement = kAudioObjectPropertyElementMain,
  };

  CFStringRef cf_name = nullptr;
  UInt32 size = sizeof(cf_name);
  const OSStatus status = AudioObjectGetPropertyData(device_id, &address, 0, nullptr,
                                                     &size, &cf_name);
  if (status != noErr || cf_name == nullptr) {
    return false;
  }

  *name = cfStringToUtf8(cf_name);
  CFRelease(cf_name);
  return true;
}

bool hasOutputChannels(AudioDeviceID device_id) {
  AudioObjectPropertyAddress address{
      .mSelector = kAudioDevicePropertyStreamConfiguration,
      .mScope = kAudioDevicePropertyScopeOutput,
      .mElement = kAudioObjectPropertyElementMain,
  };

  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device_id, &address, 0, nullptr, &size) != noErr ||
      size == 0) {
    return false;
  }

  std::vector<std::uint8_t> buffer(size);
  auto* list = reinterpret_cast<AudioBufferList*>(buffer.data());
  if (AudioObjectGetPropertyData(device_id, &address, 0, nullptr, &size, list) != noErr) {
    return false;
  }

  UInt32 channels = 0;
  for (UInt32 index = 0; index < list->mNumberBuffers; ++index) {
    channels += list->mBuffers[index].mNumberChannels;
  }
  return channels > 0;
}

bool getDefaultOutputDevice(AudioDeviceID* device_id) {
  if (device_id == nullptr) {
    return false;
  }

  AudioObjectPropertyAddress address{
      .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
      .mScope = kAudioObjectPropertyScopeGlobal,
      .mElement = kAudioObjectPropertyElementMain,
  };

  AudioDeviceID detected = kAudioObjectUnknown;
  UInt32 size = sizeof(detected);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                 &address,
                                 0,
                                 nullptr,
                                 &size,
                                 &detected) != noErr) {
    return false;
  }

  if (detected == kAudioObjectUnknown || !hasOutputChannels(detected)) {
    return false;
  }

  *device_id = detected;
  return true;
}

class CoreAudioBackend final : public AudioBackend {
 public:
  ~CoreAudioBackend() override { stop(); }

  bool start(const AudioBackendConfig& config,
             Callback callback,
             std::string* error_message) override {
    if (running_.load(std::memory_order_acquire)) {
      return true;
    }

    if (config.sample_rate_hz == 0 || config.buffer_size_frames == 0 ||
        callback == nullptr) {
      if (error_message != nullptr) {
        *error_message = "invalid CoreAudio backend configuration";
      }
      return false;
    }

    // Store callback BEFORE starting the audio unit so the render
    // callback never sees a null function pointer.
    config_ = config;
    callback_ = std::move(callback);

    // Resolve the target device.
    AudioDeviceID target_device = kAudioObjectUnknown;

    if (!config_.device_id.empty() && config_.device_id != "default") {
      try {
        const auto parsed = static_cast<AudioDeviceID>(
            std::stoul(config_.device_id));
        if (parsed != kAudioObjectUnknown && hasOutputChannels(parsed)) {
          target_device = parsed;
        }
      } catch (...) {}
    }

    if (target_device == kAudioObjectUnknown) {
      if (!getDefaultOutputDevice(&target_device)) {
        if (error_message != nullptr) {
          *error_message = "no default output device found";
        }
        callback_ = nullptr;
        return false;
      }
    }

    std::string dev_name;
    getDeviceName(target_device, &dev_name);
    NSLog(@"[FF-CA] Using device %u '%s'", target_device, dev_name.c_str());

    AudioComponentDescription description{};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr) {
      if (error_message != nullptr) {
        *error_message = "failed to find HAL output audio component";
      }
      callback_ = nullptr;
      return false;
    }

    OSStatus status = AudioComponentInstanceNew(component, &audio_unit_);
    if (status != noErr || audio_unit_ == nullptr) {
      if (error_message != nullptr) {
        *error_message = "failed to instantiate HAL output audio unit";
      }
      callback_ = nullptr;
      return false;
    }

    // Enable output on bus 0, disable input on bus 1.
    UInt32 enable_output = 1;
    status = AudioUnitSetProperty(audio_unit_,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output,
                                  0,
                                  &enable_output,
                                  sizeof(enable_output));
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed enabling output on HAL audio unit";
      }
      callback_ = nullptr;
      return false;
    }

    UInt32 disable_input = 0;
    status = AudioUnitSetProperty(audio_unit_,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Input,
                                  1,
                                  &disable_input,
                                  sizeof(disable_input));
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed disabling input on HAL audio unit";
      }
      callback_ = nullptr;
      return false;
    }

    // Set the output device.
    status = AudioUnitSetProperty(audio_unit_,
                                  kAudioOutputUnitProperty_CurrentDevice,
                                  kAudioUnitScope_Global,
                                  0,
                                  &target_device,
                                  sizeof(target_device));
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed setting output device";
      }
      callback_ = nullptr;
      return false;
    }

    // Query the hardware's native stream format to match sample rate.
    AudioStreamBasicDescription hw_format{};
    UInt32 hw_format_size = sizeof(hw_format);
    status = AudioUnitGetProperty(audio_unit_,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Output,
                                  0,
                                  &hw_format,
                                  &hw_format_size);
    if (status == noErr && hw_format.mSampleRate > 0) {
      NSLog(@"[FF-CA] Hardware format: rate=%.0f ch=%u bits=%u flags=0x%x",
            hw_format.mSampleRate, hw_format.mChannelsPerFrame,
            hw_format.mBitsPerChannel, hw_format.mFormatFlags);
      config_.sample_rate_hz = static_cast<std::uint32_t>(hw_format.mSampleRate);
    }

    // Set render callback.
    AURenderCallbackStruct callback_struct{};
    callback_struct.inputProc = &CoreAudioBackend::renderCallback;
    callback_struct.inputProcRefCon = this;
    status = AudioUnitSetProperty(audio_unit_,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input,
                                  0,
                                  &callback_struct,
                                  sizeof(callback_struct));
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed setting CoreAudio render callback";
      }
      callback_ = nullptr;
      return false;
    }

    // Set our desired stream format on the input scope (what we provide).
    AudioStreamBasicDescription stream{};
    stream.mSampleRate = static_cast<Float64>(config_.sample_rate_hz);
    stream.mFormatID = kAudioFormatLinearPCM;
    stream.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    stream.mBitsPerChannel = 32;
    stream.mChannelsPerFrame = 2;
    stream.mFramesPerPacket = 1;
    stream.mBytesPerFrame = sizeof(float) * 2;
    stream.mBytesPerPacket = stream.mBytesPerFrame;

    status = AudioUnitSetProperty(audio_unit_,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &stream,
                                  sizeof(stream));
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed setting CoreAudio stream format";
      }
      callback_ = nullptr;
      return false;
    }

    // Query the device's current I/O buffer frame size so we can set
    // MaximumFramesPerSlice large enough.  If the AudioUnit is asked to
    // render more frames than this limit the render callback is never
    // invoked and CoreAudio logs kAudioUnitErr_TooManyFramesToProcess
    // (-10874).
    UInt32 hw_buffer_frames = 0;
    UInt32 hw_buf_size = sizeof(hw_buffer_frames);
    AudioObjectPropertyAddress buf_address{
        .mSelector = kAudioDevicePropertyBufferFrameSize,
        .mScope = kAudioDevicePropertyScopeOutput,
        .mElement = kAudioObjectPropertyElementMain,
    };
    if (AudioObjectGetPropertyData(target_device, &buf_address, 0, nullptr,
                                   &hw_buf_size, &hw_buffer_frames) == noErr &&
        hw_buffer_frames > 0) {
      NSLog(@"[FF-CA] Hardware buffer frame size: %u", hw_buffer_frames);
    } else {
      hw_buffer_frames = 1024;
      NSLog(@"[FF-CA] Could not query hardware buffer size, defaulting to %u", hw_buffer_frames);
    }

    // Use the larger of the hardware buffer size and our requested size,
    // with a floor of 512 to avoid edge cases on modern macOS.
    UInt32 max_frames = std::max({hw_buffer_frames,
                                  config_.buffer_size_frames,
                                  static_cast<UInt32>(512)});
    status = AudioUnitSetProperty(audio_unit_,
                                  kAudioUnitProperty_MaximumFramesPerSlice,
                                  kAudioUnitScope_Global,
                                  0,
                                  &max_frames,
                                  sizeof(max_frames));
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed setting CoreAudio max frames per slice";
      }
      callback_ = nullptr;
      return false;
    }
    NSLog(@"[FF-CA] MaximumFramesPerSlice set to %u", max_frames);

    status = AudioUnitInitialize(audio_unit_);
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed to initialize CoreAudio unit";
      }
      callback_ = nullptr;
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      stats_ = AudioBackendStats{};
      last_callback_started_ = {};
    }

    status = AudioOutputUnitStart(audio_unit_);
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed to start CoreAudio output";
      }
      callback_ = nullptr;
      return false;
    }

    NSLog(@"[FF-CA] CoreAudio started: device=%u '%s' rate=%u",
          target_device, dev_name.c_str(), config_.sample_rate_hz);
    running_.store(true, std::memory_order_release);
    return true;
  }

  void stop() override {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    if (audio_unit_ != nullptr) {
      AudioOutputUnitStop(audio_unit_);
    }
    cleanupAudioUnit();
  }

  [[nodiscard]] bool isRunning() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::vector<AudioDeviceInfo> outputDevices() const override {
    std::vector<AudioDeviceInfo> devices;

    AudioObjectPropertyAddress default_address{
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };

    AudioDeviceID default_device = kAudioObjectUnknown;
    UInt32 default_size = sizeof(default_device);
    AudioObjectGetPropertyData(kAudioObjectSystemObject,
                               &default_address,
                               0,
                               nullptr,
                               &default_size,
                               &default_device);

    AudioObjectPropertyAddress address{
        .mSelector = kAudioHardwarePropertyDevices,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };

    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                       &address,
                                       0,
                                       nullptr,
                                       &size) != noErr ||
        size == 0) {
      return {{.id = "default", .name = "Default Output", .is_default = true}};
    }

    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                   &address,
                                   0,
                                   nullptr,
                                   &size,
                                   ids.data()) != noErr) {
      return {{.id = "default", .name = "Default Output", .is_default = true}};
    }

    for (AudioDeviceID device_id : ids) {
      if (!hasOutputChannels(device_id)) {
        continue;
      }

      std::string name;
      if (!getDeviceName(device_id, &name)) {
        name = "Output Device " + std::to_string(device_id);
      }

      devices.push_back(AudioDeviceInfo{
          .id = std::to_string(device_id),
          .name = std::move(name),
          .is_default = device_id == default_device,
      });
    }

    if (devices.empty()) {
      devices.push_back(
          AudioDeviceInfo{.id = "default", .name = "Default Output", .is_default = true});
    }

    return devices;
  }

  [[nodiscard]] AudioBackendStats stats() const noexcept override {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
  }

  [[nodiscard]] std::uint32_t actualSampleRate() const noexcept override {
    return config_.sample_rate_hz;
  }

 private:
  static OSStatus renderCallback(void* in_ref_con,
                                 AudioUnitRenderActionFlags* /*io_action_flags*/,
                                 const AudioTimeStamp* /*in_time_stamp*/,
                                 UInt32 /*in_bus_number*/,
                                 UInt32 in_number_frames,
                                 AudioBufferList* io_data) {
    auto* self = reinterpret_cast<CoreAudioBackend*>(in_ref_con);
    if (self == nullptr) {
      return noErr;
    }
    return self->onRender(in_number_frames, io_data);
  }

  OSStatus onRender(UInt32 frames, AudioBufferList* io_data) {
    if (io_data == nullptr || frames == 0 || callback_ == nullptr) {
      return noErr;
    }

    const auto started = std::chrono::steady_clock::now();

    if (io_data->mNumberBuffers == 1 &&
        io_data->mBuffers[0].mData != nullptr &&
        io_data->mBuffers[0].mDataByteSize >= frames * sizeof(float) * 2U) {
      auto* interleaved = reinterpret_cast<float*>(io_data->mBuffers[0].mData);
      callback_(interleaved, frames);
    } else {
      const std::size_t required = static_cast<std::size_t>(frames) * 2U;
      if (interleaved_scratch_.size() < required) {
        interleaved_scratch_.resize(required, 0.0F);
      }
      callback_(interleaved_scratch_.data(), frames);

      for (UInt32 frame = 0; frame < frames; ++frame) {
        const float left = interleaved_scratch_[frame * 2U + 0U];
        const float right = interleaved_scratch_[frame * 2U + 1U];
        for (UInt32 buffer = 0; buffer < io_data->mNumberBuffers; ++buffer) {
          auto* output = reinterpret_cast<float*>(io_data->mBuffers[buffer].mData);
          if (output == nullptr) {
            continue;
          }
          const UInt32 channels = std::max<UInt32>(1, io_data->mBuffers[buffer].mNumberChannels);
          if (channels == 1) {
            output[frame] = (left + right) * 0.5F;
          } else {
            output[frame * channels + 0] = left;
            output[frame * channels + 1] = right;
          }
        }
      }
    }

    const auto ended = std::chrono::steady_clock::now();
    const double callback_duration_us = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(ended - started).count());

    const double budget_us = static_cast<double>(frames) * 1'000'000.0 /
                             static_cast<double>(std::max<std::uint32_t>(1U, config_.sample_rate_hz));

    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.callback_count += 1;
    stats_.average_callback_duration_us +=
        (callback_duration_us - stats_.average_callback_duration_us) /
        static_cast<double>(stats_.callback_count);
    stats_.peak_callback_duration_us =
        std::max(stats_.peak_callback_duration_us, callback_duration_us);

    if (last_callback_started_.time_since_epoch().count() != 0) {
      const double callback_interval_us = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              started - last_callback_started_)
              .count());
      stats_.average_callback_interval_us +=
          (callback_interval_us - stats_.average_callback_interval_us) /
          static_cast<double>(stats_.callback_count);
      stats_.peak_callback_interval_us =
          std::max(stats_.peak_callback_interval_us, callback_interval_us);
      if (callback_interval_us > (budget_us * 1.5)) {
        stats_.xrun_count += 1;
      }
    }

    if (callback_duration_us > (budget_us * 0.95)) {
      stats_.xrun_count += 1;
    }

    last_callback_started_ = started;
    return noErr;
  }

  void cleanupAudioUnit() {
    if (audio_unit_ == nullptr) {
      return;
    }

    AudioUnitUninitialize(audio_unit_);
    AudioComponentInstanceDispose(audio_unit_);
    audio_unit_ = nullptr;
  }

  AudioUnit audio_unit_ = nullptr;

  AudioBackendConfig config_{};
  Callback callback_{};

  std::atomic<bool> running_{false};

  std::vector<float> interleaved_scratch_;

  mutable std::mutex stats_mutex_;
  AudioBackendStats stats_{};
  std::chrono::steady_clock::time_point last_callback_started_{};
};

}  // namespace

std::unique_ptr<AudioBackend> createAudioBackend() {
  return std::make_unique<CoreAudioBackend>();
}

}  // namespace ff::desktop

#endif
