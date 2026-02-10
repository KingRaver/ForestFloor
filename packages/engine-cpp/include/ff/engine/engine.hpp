#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ff/abi/contracts.h"
#include "ff/dsp/gain.hpp"

namespace ff::engine {

struct AudioDeviceConfig final {
  std::string device_id = "default";
  std::uint32_t sample_rate_hz = 48000;
  std::uint32_t buffer_size_frames = 256;
};

struct TransportState final {
  float bpm = 120.0F;
  bool is_playing = false;
};

struct TrackParameters final {
  float gain = 1.0F;
  float pan = 0.0F;
  float filter_cutoff = 1.0F;
  float envelope_decay = 1.0F;
  float pitch_semitones = 0.0F;
  int choke_group = -1;
};

class Engine final {
 public:
  static constexpr std::size_t kTrackCount = 8;
  static constexpr std::uint8_t kDefaultPadBaseNote = 36;

  Engine() = default;

  void setMasterGain(float gain) noexcept;
  void process(float* mono_buffer, std::size_t frames) noexcept;

  bool setTrackSample(std::size_t track_index, std::vector<float> sample);
  void clearTrackSample(std::size_t track_index) noexcept;
  bool triggerTrack(std::size_t track_index, float velocity) noexcept;
  bool setTrackParameters(std::size_t track_index, TrackParameters parameters) noexcept;
  [[nodiscard]] TrackParameters trackParameters(std::size_t track_index) const noexcept;
  bool applyParameterUpdate(std::uint32_t parameter_id, float normalized_value) noexcept;

  bool handleMidiNoteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
  void setPadBaseNote(std::uint8_t base_note) noexcept;
  [[nodiscard]] std::uint8_t padBaseNote() const noexcept;

  void startTransport() noexcept;
  void stopTransport() noexcept;
  [[nodiscard]] bool isTransportRunning() const noexcept;
  void setTempoBpm(float bpm) noexcept;
  [[nodiscard]] float tempoBpm() const noexcept;

  bool setAudioDeviceConfig(AudioDeviceConfig config);
  [[nodiscard]] AudioDeviceConfig audioDeviceConfig() const;

 private:
  struct TrackVoice final {
    std::vector<float> sample;
    double playhead = 0.0;
    float trigger_velocity = 0.0F;
    float envelope_value = 0.0F;
    float filter_state = 0.0F;
    bool active = false;
    TrackParameters parameters;
  };

  static float clampGain(float gain) noexcept;
  static float clampPan(float pan) noexcept;
  static float clampFilterCutoff(float cutoff) noexcept;
  static float clampEnvelopeDecay(float decay) noexcept;
  static float clampPitchSemitones(float semitones) noexcept;
  static int clampChokeGroup(int choke_group) noexcept;
  [[nodiscard]] float sampleAt(const TrackVoice& track) const noexcept;
  [[nodiscard]] float pitchRatio(float semitones) const noexcept;
  [[nodiscard]] float filterAlpha(float cutoff) const noexcept;
  [[nodiscard]] float envelopeCoefficient(float decay) const noexcept;
  [[nodiscard]] float panGain(float pan) const noexcept;

  static float clampVelocity(float velocity) noexcept;

  ff::dsp::GainProcessor master_gain_;
  TrackVoice tracks_[kTrackCount];
  std::uint8_t pad_base_note_ = kDefaultPadBaseNote;
  TransportState transport_;
  AudioDeviceConfig audio_device_config_;
};

}  // namespace ff::engine
