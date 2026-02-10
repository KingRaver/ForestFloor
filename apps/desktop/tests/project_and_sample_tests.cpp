#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

#include "project_io.hpp"
#include "sample_loader.hpp"

namespace {

bool almostEqual(float left, float right, float epsilon = 0.0001F) {
  return std::fabs(left - right) <= epsilon;
}

void starterKitSamplesLoad() {
  const std::filesystem::path kick_path =
      std::filesystem::path(FF_SOURCE_ROOT) / "assets" / "starter-kit" / "kick.wav";

  ff::desktop::LoadedSample sample;
  std::string error;
  assert(ff::desktop::loadMonoSample(kick_path, 48'000, &sample, &error));
  assert(sample.source_sample_rate_hz == 48'000);
  assert(!sample.mono.empty());

  float peak = 0.0F;
  for (float value : sample.mono) {
    assert(std::isfinite(value));
    peak = std::max(peak, std::fabs(value));
  }
  assert(peak > 0.01F);
}

void projectRoundTripPreservesCoreState() {
  ff::desktop::ProjectModel original;
  original.name = "desktop-roundtrip";
  original.bpm = 138.0F;
  original.swing = 0.22F;

  original.tracks[0].sample_path =
      (std::filesystem::path(FF_SOURCE_ROOT) / "assets" / "starter-kit" / "kick.wav").string();
  original.tracks[0].parameters.gain = 1.5F;
  original.tracks[0].parameters.pan = -0.2F;
  original.tracks[0].parameters.filter_cutoff = 0.7F;
  original.tracks[0].parameters.envelope_decay = 0.6F;
  original.tracks[0].parameters.pitch_semitones = -3.0F;
  original.tracks[0].parameters.choke_group = 2;

  original.pattern[0][0] = ff::desktop::ProjectStep{.active = true, .velocity = 127};
  original.pattern[1][4] = ff::desktop::ProjectStep{.active = true, .velocity = 110};

  const auto file_path = std::filesystem::temp_directory_path() /
                         "forest_floor_desktop_project_roundtrip.ffproject";

  std::string save_error;
  assert(ff::desktop::saveProjectToFile(file_path, original, &save_error));

  ff::desktop::ProjectModel restored;
  std::string load_error;
  assert(ff::desktop::loadProjectFromFile(file_path, &restored, &load_error));

  assert(restored.name == original.name);
  assert(almostEqual(restored.bpm, original.bpm));
  assert(almostEqual(restored.swing, original.swing));
  assert(restored.tracks[0].sample_path == original.tracks[0].sample_path);
  assert(almostEqual(restored.tracks[0].parameters.gain, original.tracks[0].parameters.gain));
  assert(almostEqual(restored.tracks[0].parameters.pan, original.tracks[0].parameters.pan));
  assert(almostEqual(restored.tracks[0].parameters.filter_cutoff,
                     original.tracks[0].parameters.filter_cutoff));
  assert(almostEqual(restored.tracks[0].parameters.envelope_decay,
                     original.tracks[0].parameters.envelope_decay));
  assert(almostEqual(restored.tracks[0].parameters.pitch_semitones,
                     original.tracks[0].parameters.pitch_semitones));
  assert(restored.tracks[0].parameters.choke_group == original.tracks[0].parameters.choke_group);
  assert(restored.pattern[0][0].active);
  assert(restored.pattern[0][0].velocity == 127);
  assert(restored.pattern[1][4].active);
  assert(restored.pattern[1][4].velocity == 110);

  std::error_code remove_error;
  std::filesystem::remove(file_path, remove_error);
}

}  // namespace

int main() {
  starterKitSamplesLoad();
  projectRoundTripPreservesCoreState();
  return 0;
}
