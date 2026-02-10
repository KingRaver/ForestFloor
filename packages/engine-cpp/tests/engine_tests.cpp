#include "ff/engine/engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <vector>

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

}  // namespace

int main() {
  samplePlaybackMixesTriggeredTrack();
  masterGainScalesOutput();
  midiNoteOnTriggersPadTrack();
  transportAndAudioDeviceConfigRoundTrip();
  return 0;
}

