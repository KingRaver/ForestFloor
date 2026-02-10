#include "ff/engine/engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <vector>

#include "ff/abi/contracts.h"

namespace {

bool almostEqual(float left, float right, float epsilon = 0.0001F) {
  return std::fabs(left - right) <= epsilon;
}

void samplePlaybackMixesTriggeredTrack() {
  ff::engine::Engine engine;
  assert(engine.setTrackSample(0, std::vector<float>{1.0F, 0.5F}));
  assert(engine.triggerTrack(0, 1.0F));

  std::array<float, 4> buffer{};
  engine.process(buffer.data(), buffer.size());

  assert(almostEqual(buffer[0], 1.0F));
  assert(almostEqual(buffer[1], 0.5F));
  assert(almostEqual(buffer[2], 0.0F));
  assert(almostEqual(buffer[3], 0.0F));
}

void masterGainScalesOutput() {
  ff::engine::Engine engine;
  assert(engine.setTrackSample(0, std::vector<float>{1.0F}));
  assert(engine.triggerTrack(0, 1.0F));
  engine.setMasterGain(0.25F);

  std::array<float, 1> buffer{};
  engine.process(buffer.data(), buffer.size());
  assert(almostEqual(buffer[0], 0.25F));
}

void midiNoteOnTriggersPadTrack() {
  ff::engine::Engine engine;
  assert(engine.setTrackSample(2, std::vector<float>{0.8F}));
  assert(engine.handleMidiNoteOn(38, 127));  // base note 36 + track index 2

  std::array<float, 1> buffer{};
  engine.process(buffer.data(), buffer.size());
  assert(almostEqual(buffer[0], 0.8F));
}

void transportAndAudioDeviceConfigRoundTrip() {
  ff::engine::Engine engine;
  assert(!engine.isTransportRunning());
  engine.startTransport();
  assert(engine.isTransportRunning());
  engine.stopTransport();
  assert(!engine.isTransportRunning());

  engine.setTempoBpm(400.0F);
  assert(almostEqual(engine.tempoBpm(), 300.0F));
  engine.setTempoBpm(10.0F);
  assert(almostEqual(engine.tempoBpm(), 20.0F));

  ff::engine::AudioDeviceConfig config;
  config.device_id = "test-device";
  config.sample_rate_hz = 44100;
  config.buffer_size_frames = 128;
  assert(engine.setAudioDeviceConfig(config));
  const auto restored = engine.audioDeviceConfig();
  assert(restored.device_id == "test-device");
  assert(restored.sample_rate_hz == 44100);
  assert(restored.buffer_size_frames == 128);

  ff::engine::AudioDeviceConfig invalid;
  invalid.sample_rate_hz = 0;
  invalid.buffer_size_frames = 128;
  assert(!engine.setAudioDeviceConfig(invalid));
}

void trackParametersAffectOutput() {
  ff::engine::Engine engine;
  assert(engine.setTrackSample(0, std::vector<float>{1.0F, 1.0F, 1.0F, 1.0F}));

  ff::engine::TrackParameters parameters;
  parameters.gain = 0.5F;
  parameters.pan = 1.0F;
  parameters.filter_cutoff = 0.0F;
  parameters.envelope_decay = 1.0F;
  parameters.pitch_semitones = 0.0F;
  assert(engine.setTrackParameters(0, parameters));
  assert(engine.triggerTrack(0, 1.0F));

  std::array<float, 1> buffer{};
  engine.process(buffer.data(), buffer.size());

  // With gain 0.5, pan edge attenuation 0.5, and one-pole low cutoff alpha 0.01.
  assert(almostEqual(buffer[0], 0.0025F, 0.001F));
}

void chokeGroupsSilencePreviousTrack() {
  ff::engine::Engine engine;
  assert(engine.setTrackSample(0, std::vector<float>{1.0F, 1.0F}));
  assert(engine.setTrackSample(1, std::vector<float>{1.0F, 1.0F}));

  ff::engine::TrackParameters choke;
  choke.choke_group = 2;
  assert(engine.setTrackParameters(0, choke));
  assert(engine.setTrackParameters(1, choke));

  assert(engine.triggerTrack(0, 1.0F));
  assert(engine.triggerTrack(1, 1.0F));

  std::array<float, 1> buffer{};
  engine.process(buffer.data(), buffer.size());
  assert(almostEqual(buffer[0], 1.0F));
}

void pitchControlChangesPlaybackRate() {
  ff::engine::Engine engine;
  assert(engine.setTrackSample(0, std::vector<float>{1.0F, 0.0F, 1.0F, 0.0F}));

  ff::engine::TrackParameters normal;
  normal.pitch_semitones = 0.0F;
  assert(engine.setTrackParameters(0, normal));
  assert(engine.triggerTrack(0, 1.0F));
  std::array<float, 2> normal_buffer{};
  engine.process(normal_buffer.data(), normal_buffer.size());

  ff::engine::TrackParameters raised;
  raised.pitch_semitones = 12.0F;
  assert(engine.setTrackParameters(0, raised));
  assert(engine.triggerTrack(0, 1.0F));
  std::array<float, 2> raised_buffer{};
  engine.process(raised_buffer.data(), raised_buffer.size());

  assert(normal_buffer[1] < raised_buffer[1]);
}

void parameterUpdatesMapNormalizedValuesToTrackParameters() {
  ff::engine::Engine engine;

  const auto gain_id =
      static_cast<std::uint32_t>(FF_PARAM_TRACK_BASE) + static_cast<std::uint32_t>(FF_PARAM_SLOT_GAIN);
  const auto pan_id =
      static_cast<std::uint32_t>(FF_PARAM_TRACK_BASE) + static_cast<std::uint32_t>(FF_PARAM_SLOT_PAN);
  const auto pitch_id =
      static_cast<std::uint32_t>(FF_PARAM_TRACK_BASE) + static_cast<std::uint32_t>(FF_PARAM_SLOT_PITCH);
  const auto choke_id = static_cast<std::uint32_t>(FF_PARAM_TRACK_BASE) +
                        static_cast<std::uint32_t>(FF_PARAM_SLOT_CHOKE_GROUP);

  assert(engine.applyParameterUpdate(gain_id, 0.5F));
  assert(engine.applyParameterUpdate(pan_id, 0.75F));
  assert(engine.applyParameterUpdate(pitch_id, 0.75F));
  assert(engine.applyParameterUpdate(choke_id, 0.25F));

  const auto restored = engine.trackParameters(0);
  assert(almostEqual(restored.gain, 1.0F));
  assert(almostEqual(restored.pan, 0.5F));
  assert(almostEqual(restored.pitch_semitones, 12.0F));
  assert(restored.choke_group == 3);
}

void invalidParameterUpdateIsRejected() {
  ff::engine::Engine engine;
  assert(!engine.applyParameterUpdate(0x9999, 0.5F));
  const auto out_of_range_id = static_cast<std::uint32_t>(FF_PARAM_TRACK_BASE) +
                               (8U * static_cast<std::uint32_t>(FF_PARAM_TRACK_STRIDE)) +
                               static_cast<std::uint32_t>(FF_PARAM_SLOT_GAIN);
  assert(!engine.applyParameterUpdate(out_of_range_id, 0.5F));
}

}  // namespace

int main() {
  samplePlaybackMixesTriggeredTrack();
  masterGainScalesOutput();
  midiNoteOnTriggersPadTrack();
  transportAndAudioDeviceConfigRoundTrip();
  trackParametersAffectOutput();
  chokeGroupsSilencePreviousTrack();
  pitchControlChangesPlaybackRate();
  parameterUpdatesMapNormalizedValuesToTrackParameters();
  invalidParameterUpdateIsRejected();
  return 0;
}
