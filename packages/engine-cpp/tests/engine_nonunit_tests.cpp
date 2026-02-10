#include "ff/engine/engine.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <vector>

namespace {

bool almostEqual(float left, float right, float epsilon = 0.0001F) {
  return std::fabs(left - right) <= epsilon;
}

std::vector<float> sineSample(std::size_t length, float amplitude, float phase) {
  constexpr float kTwoPi = 6.28318530717958647692F;
  std::vector<float> output(length, 0.0F);
  for (std::size_t index = 0; index < length; ++index) {
    const float normalized = static_cast<float>(index) / static_cast<float>(length);
    output[index] = std::sin((normalized * kTwoPi) + phase) * amplitude;
  }

  return output;
}

void goldenRenderMatchesReferenceOutput() {
  ff::engine::Engine engine;
  ff::engine::AudioDeviceConfig config;
  config.sample_rate_hz = 48'000;
  config.buffer_size_frames = 256;
  assert(engine.setAudioDeviceConfig(config));

  ff::engine::TrackParameters params;
  params.gain = 1.0F;
  params.pan = 0.0F;
  params.filter_cutoff = 1.0F;
  params.envelope_decay = 1.0F;
  params.pitch_semitones = 0.0F;
  params.choke_group = -1;
  assert(engine.setTrackParameters(0, params));
  assert(engine.setTrackSample(0, std::vector<float>{1.0F, 0.5F, -0.25F, 0.25F}));
  assert(engine.triggerTrack(0, 1.0F));

  std::array<float, 8> rendered{};
  engine.process(rendered.data(), rendered.size());

  const std::array<float, 8> reference = {
      1.0F, 0.49999654F, -0.24999654F, 0.24999483F, 0.0F, 0.0F, 0.0F, 0.0F,
  };
  for (std::size_t index = 0; index < reference.size(); ++index) {
    assert(almostEqual(rendered[index], reference[index], 0.001F));
  }
}

void stressRenderRemainsFiniteWithBoundedRuntime() {
  ff::engine::Engine engine;
  ff::engine::AudioDeviceConfig config;
  config.sample_rate_hz = 48'000;
  config.buffer_size_frames = 128;
  assert(engine.setAudioDeviceConfig(config));

  for (std::size_t track_index = 0; track_index < ff::engine::Engine::kTrackCount; ++track_index) {
    const float amplitude = 0.4F + (0.05F * static_cast<float>(track_index));
    const float phase = static_cast<float>(track_index) * 0.31F;
    assert(engine.setTrackSample(track_index, sineSample(1'024, amplitude, phase)));

    ff::engine::TrackParameters params;
    params.gain = 0.6F + (0.1F * static_cast<float>(track_index % 4));
    params.pan = (static_cast<float>(track_index) - 3.5F) / 3.5F;
    params.filter_cutoff = 0.6F + (0.05F * static_cast<float>(track_index % 3));
    params.envelope_decay = 0.3F + (0.1F * static_cast<float>(track_index % 5));
    params.pitch_semitones = static_cast<float>((static_cast<int>(track_index) % 5) - 2);
    params.choke_group = -1;
    assert(engine.setTrackParameters(track_index, params));
  }

  std::array<float, 128> block{};
  float observed_peak = 0.0F;

  const auto started = std::chrono::steady_clock::now();
  for (std::size_t block_index = 0; block_index < 6'000; ++block_index) {
    if (block_index % 24 == 0) {
      for (std::size_t track_index = 0; track_index < ff::engine::Engine::kTrackCount; ++track_index) {
        const float velocity = 0.4F + (0.07F * static_cast<float>(track_index));
        assert(engine.triggerTrack(track_index, velocity));
      }
    }

    engine.process(block.data(), block.size());
    for (float sample : block) {
      assert(std::isfinite(sample));
      observed_peak = std::max(observed_peak, std::fabs(sample));
    }
  }
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
          .count();

  // Stress path should stay finite and complete comfortably under this broad regression bound.
  assert(observed_peak < 16.0F);
  assert(elapsed_ms < 10'000);
}

}  // namespace

int main() {
  goldenRenderMatchesReferenceOutput();
  stressRenderRemainsFiniteWithBoundedRuntime();
  return 0;
}
