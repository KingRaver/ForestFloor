#include "ff/engine/engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct ProfileOptions final {
  std::size_t blocks = 1'024;
  std::size_t frames = 256;
  std::string output_path;
};

bool parsePositiveSize(const std::string& text, std::size_t* value) {
  if (value == nullptr) {
    return false;
  }

  try {
    const auto parsed = std::stoull(text);
    if (parsed == 0) {
      return false;
    }
    *value = static_cast<std::size_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

bool parseArgs(int argc, char** argv, ProfileOptions* options) {
  if (options == nullptr) {
    return false;
  }

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--blocks") {
      if (index + 1 >= argc || !parsePositiveSize(argv[index + 1], &options->blocks)) {
        std::cerr << "Invalid --blocks value\n";
        return false;
      }
      index += 1;
      continue;
    }

    if (arg == "--frames") {
      if (index + 1 >= argc || !parsePositiveSize(argv[index + 1], &options->frames)) {
        std::cerr << "Invalid --frames value\n";
        return false;
      }
      index += 1;
      continue;
    }

    if (arg == "--output") {
      if (index + 1 >= argc) {
        std::cerr << "Missing --output path\n";
        return false;
      }
      options->output_path = argv[index + 1];
      index += 1;
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    return false;
  }

  return true;
}

std::vector<float> sineSample(std::size_t length, float amplitude, float phase) {
  constexpr float kTwoPi = 6.28318530717958647692F;
  std::vector<float> output(length, 0.0F);
  for (std::size_t frame = 0; frame < length; ++frame) {
    const float normalized = static_cast<float>(frame) / static_cast<float>(length);
    output[frame] = std::sin((normalized * kTwoPi) + phase) * amplitude;
  }

  return output;
}

std::string toProfileJson(const ff::engine::PerformanceStats& stats,
                          std::size_t blocks_requested,
                          std::size_t frames_requested) {
  std::string json;
  json.reserve(512);
  json += "{\n";
  json += "  \"blocks_requested\": " + std::to_string(blocks_requested) + ",\n";
  json += "  \"frames_per_block\": " + std::to_string(frames_requested) + ",\n";
  json += "  \"processed_blocks\": " + std::to_string(stats.processed_blocks) + ",\n";
  json += "  \"processed_frames\": " + std::to_string(stats.processed_frames) + ",\n";
  json += "  \"xrun_count\": " + std::to_string(stats.xrun_count) + ",\n";
  json += "  \"average_block_duration_us\": " + std::to_string(stats.average_block_duration_us) + ",\n";
  json += "  \"peak_block_duration_us\": " + std::to_string(stats.peak_block_duration_us) + ",\n";
  json += "  \"average_callback_utilization\": " +
          std::to_string(stats.average_callback_utilization) + ",\n";
  json += "  \"peak_callback_utilization\": " + std::to_string(stats.peak_callback_utilization) + "\n";
  json += "}\n";
  return json;
}

}  // namespace

int main(int argc, char** argv) {
  ProfileOptions options{};
  if (!parseArgs(argc, argv, &options)) {
    return 1;
  }

  ff::engine::Engine engine;
  ff::engine::AudioDeviceConfig config;
  config.sample_rate_hz = 48'000;
  config.buffer_size_frames = static_cast<std::uint32_t>(std::min(options.frames, std::size_t{1'024}));
  if (!engine.setAudioDeviceConfig(config)) {
    std::cerr << "Failed to set audio config\n";
    return 1;
  }

  for (std::size_t track_index = 0; track_index < ff::engine::Engine::kTrackCount; ++track_index) {
    const float amplitude = 0.45F + (0.05F * static_cast<float>(track_index));
    const float phase = static_cast<float>(track_index) * 0.23F;
    if (!engine.setTrackSample(track_index, sineSample(2'048, amplitude, phase))) {
      std::cerr << "Failed to set sample on track " << track_index << "\n";
      return 1;
    }

    ff::engine::TrackParameters params;
    params.gain = 0.7F + (0.08F * static_cast<float>(track_index % 3));
    params.pan = (static_cast<float>(track_index) - 3.5F) / 3.5F;
    params.filter_cutoff = 0.5F + (0.1F * static_cast<float>(track_index % 4));
    params.envelope_decay = 0.25F + (0.12F * static_cast<float>(track_index % 5));
    params.pitch_semitones = static_cast<float>((static_cast<int>(track_index) % 7) - 3);
    params.choke_group = -1;
    if (!engine.setTrackParameters(track_index, params)) {
      std::cerr << "Failed to set parameters on track " << track_index << "\n";
      return 1;
    }
  }

  engine.setProfilingEnabled(true);
  engine.resetPerformanceStats();

  std::vector<float> block(options.frames, 0.0F);
  for (std::size_t block_index = 0; block_index < options.blocks; ++block_index) {
    if (block_index % 24 == 0) {
      for (std::size_t track_index = 0; track_index < ff::engine::Engine::kTrackCount; ++track_index) {
        const float velocity = 0.4F + (0.07F * static_cast<float>(track_index));
        if (!engine.triggerTrack(track_index, velocity)) {
          std::cerr << "Failed to trigger track " << track_index << "\n";
          return 1;
        }
      }
    }

    engine.process(block.data(), block.size());
  }

  const auto stats = engine.performanceStats();
  const auto json = toProfileJson(stats, options.blocks, options.frames);

  if (!options.output_path.empty()) {
    std::ofstream output(options.output_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      std::cerr << "Failed to open output path: " << options.output_path << "\n";
      return 1;
    }
    output << json;
  }

  std::cout << json;
  return 0;
}
