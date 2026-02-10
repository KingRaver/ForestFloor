#include "audio_backend.hpp"

#if defined(APPLE)

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

    AudioComponentDescription description{};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr) {
      if (error_message != nullptr) {
        *error_message = "failed to find HAL output audio component";
      }
      return false;
    }

    OSStatus status = AudioComponentInstanceNew(component, &audio_unit_);
    if (status != noErr || audio_unit_ == nullptr) {
      if (error_message != nullptr) {
        *error_message = "failed to instantiate HAL output audio unit";
      }
      return false;
    }

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
      return false;
    }

    AudioDeviceID selected_device = kAudioObjectUnknown;
    if (!config.device_id.empty() && config.device_id != "default") {
      try {
        const auto parsed = static_cast<AudioDeviceID>(
            std::stoul(config.device_id));
        selected_device = parsed;
      } catch (...) {
        selected_device = kAudioObjectUnknown;
      }
    }

    if (selected_device != kAudioObjectUnknown) {
      status = AudioUnitSetProperty(audio_unit_,
                                    kAudioOutputUnitProperty_CurrentDevice,
                                    kAudioUnitScope_Global,
                                    0,
                                    &selected_device,
                                    sizeof(selected_device));
      if (status != noErr) {
        cleanupAudioUnit();
        if (error_message != nullptr) {
          *error_message = "failed setting selected output device";
        }
        return false;
      }
    }

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
      return false;
    }

    AudioStreamBasicDescription stream{};
    stream.mSampleRate = static_cast<Float64>(config.sample_rate_hz);
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
      return false;
    }

    UInt32 max_frames = config.buffer_size_frames;
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
      return false;
    }

    status = AudioUnitInitialize(audio_unit_);
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed to initialize CoreAudio unit";
      }
      return false;
    }

    status = AudioOutputUnitStart(audio_unit_);
    if (status != noErr) {
      cleanupAudioUnit();
      if (error_message != nullptr) {
        *error_message = "failed to start CoreAudio output";
      }
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      stats_ = AudioBackendStats{};
      last_callback_started_ = {};
    }

    config_ = config;
    callback_ = std::move(callback);
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
